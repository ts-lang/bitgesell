// Copyright (c) 2011-2020 The BGL Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/setup_common.h>

#include <banman.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/params.h>
#include <consensus/validation.h>
#include <crypto/sha256.h>
#include <init.h>
#include <interfaces/chain.h>
#include <miner.h>
#include <net.h>
#include <net_processing.h>
#include <noui.h>
#include <pow.h>
#include <rpc/blockchain.h>
#include <rpc/register.h>
#include <rpc/server.h>
#include <scheduler.h>
#include <script/sigcache.h>
#include <streams.h>
#include <txdb.h>
#include <util/memory.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/time.h>
#include <util/translation.h>
#include <util/url.h>
#include <util/vector.h>
#include <validation.h>
#include <validationinterface.h>
#include <walletinitinterface.h>
#include <functional>

const std::function<std::string(const char*)> G_TRANSLATION_FUN = nullptr;
UrlDecodeFn* const URL_DECODE = nullptr;

FastRandomContext g_insecure_rand_ctx;
/** Random context to get unique temp data dirs. Separate from g_insecure_rand_ctx, which can be seeded from a const env var */
static FastRandomContext g_insecure_rand_ctx_temp_path;

/** Return the unsigned from the environment var if available, otherwise 0 */
static uint256 GetUintFromEnv(const std::string& env_name)
{
    const char* num = std::getenv(env_name.c_str());
    if (!num) return {};
    return uint256S(num);
}

void Seed(FastRandomContext& ctx)
{
    // Should be enough to get the seed once for the process
    static uint256 seed{};
    static const std::string RANDOM_CTX_SEED{"RANDOM_CTX_SEED"};
    if (seed.IsNull()) seed = GetUintFromEnv(RANDOM_CTX_SEED);
    if (seed.IsNull()) seed = GetRandHash();
    LogPrintf("%s: Setting random seed for current tests to %s=%s\n", __func__, RANDOM_CTX_SEED, seed.GetHex());
    ctx = FastRandomContext(seed);
}

std::ostream& operator<<(std::ostream& os, const uint256& num)
{
    os << num.ToString();
    return os;
}

BasicTestingSetup::BasicTestingSetup(const std::string& chainName, const std::vector<const char*>& extra_args)
    : m_path_root{fs::temp_directory_path() / "test_common_" PACKAGE_NAME / g_insecure_rand_ctx_temp_path.rand256().ToString()}
{
    const std::vector<const char*> arguments = Cat(
        {
            "dummy",
            "-printtoconsole=0",
            "-logtimemicros",
            "-logthreadnames",
            "-debug",
            "-debugexclude=libevent",
            "-debugexclude=leveldb",
        },
        extra_args);
    util::ThreadRename("test");
    fs::create_directories(m_path_root);
    gArgs.ForceSetArg("-datadir", m_path_root.string());
    ClearDatadirCache();
    {
        SetupServerArgs(m_node);
        std::string error;
        const bool success{m_node.args->ParseParameters(arguments.size(), arguments.data(), error)};
        assert(success);
        assert(error.empty());
    }
    SelectParams(chainName);
    SeedInsecureRand();
    if (G_TEST_LOG_FUN) LogInstance().PushBackCallback(G_TEST_LOG_FUN);
    InitLogging(*m_node.args);
    AppInitParameterInteraction(*m_node.args);
    LogInstance().StartLogging();
    SHA256AutoDetect();
    ECC_Start();
    SetupEnvironment();
    SetupNetworking();
    InitSignatureCache();
    InitScriptExecutionCache();
    m_node.chain = interfaces::MakeChain(m_node);
    g_wallet_init_interface.Construct(m_node);
    fCheckBlockIndex = true;
    static bool noui_connected = false;
    if (!noui_connected) {
        noui_connect();
        noui_connected = true;
    }
}

BasicTestingSetup::~BasicTestingSetup()
{
    LogInstance().DisconnectTestLogger();
    fs::remove_all(m_path_root);
    gArgs.ClearArgs();
    ECC_Stop();
}

TestingSetup::TestingSetup(const std::string& chainName, const std::vector<const char*>& extra_args)
    : BasicTestingSetup(chainName, extra_args)
{
    const CChainParams& chainparams = Params();
    // Ideally we'd move all the RPC tests to the functional testing framework
    // instead of unit tests, but for now we need these here.
    RegisterAllCoreRPCCommands(tableRPC);

    m_node.scheduler = MakeUnique<CScheduler>();

    // We have to run a scheduler thread to prevent ActivateBestChain
    // from blocking due to queue overrun.
    threadGroup.create_thread([&] { TraceThread("scheduler", [&] { m_node.scheduler->serviceQueue(); }); });
    GetMainSignals().RegisterBackgroundSignalScheduler(*m_node.scheduler);

    pblocktree.reset(new CBlockTreeDB(1 << 20, true));

    m_node.mempool = MakeUnique<CTxMemPool>(&::feeEstimator);
    m_node.mempool->setSanityCheck(1.0);

    m_node.chainman = &::g_chainman;
    m_node.chainman->InitializeChainstate(*m_node.mempool);
    ::ChainstateActive().InitCoinsDB(
        /* cache_size_bytes */ 1 << 23, /* in_memory */ true, /* should_wipe */ false);
    assert(!::ChainstateActive().CanFlushToDisk());
    ::ChainstateActive().InitCoinsCache(1 << 23);
    assert(::ChainstateActive().CanFlushToDisk());
    if (!LoadGenesisBlock(chainparams)) {
        throw std::runtime_error("LoadGenesisBlock failed.");
    }

    BlockValidationState state;
    if (!ActivateBestChain(state, chainparams)) {
        throw std::runtime_error(strprintf("ActivateBestChain failed. (%s)", FormatStateMessage(state)));
    }

    // Start script-checking threads. Set g_parallel_script_checks to true so they are used.
    constexpr int script_check_threads = 2;
    for (int i = 0; i < script_check_threads; ++i) {
        threadGroup.create_thread([i]() { return ThreadScriptCheck(i); });
    }
    g_parallel_script_checks = true;

    m_node.banman = MakeUnique<BanMan>(GetDataDir() / "banlist.dat", nullptr, DEFAULT_MISBEHAVING_BANTIME);
    m_node.connman = MakeUnique<CConnman>(0x1337, 0x1337); // Deterministic randomness for tests.
    m_node.peer_logic = MakeUnique<PeerLogicValidation>(*m_node.connman, m_node.banman.get(), *m_node.scheduler, *m_node.chainman, *m_node.mempool);
    {
        CConnman::Options options;
        options.m_msgproc = m_node.peer_logic.get();
        m_node.connman->Init(options);
    }
}

TestingSetup::~TestingSetup()
{
    if (m_node.scheduler) m_node.scheduler->stop();
    threadGroup.interrupt_all();
    threadGroup.join_all();
    GetMainSignals().FlushBackgroundCallbacks();
    GetMainSignals().UnregisterBackgroundSignalScheduler();
    m_node.connman.reset();
    m_node.banman.reset();
    m_node.args = nullptr;
    UnloadBlockIndex(m_node.mempool.get());
    m_node.mempool.reset();
    m_node.scheduler.reset();
    m_node.chainman->Reset();
    m_node.chainman = nullptr;
    pblocktree.reset();
}

TestChain100Setup::TestChain100Setup()
{
    // Generate a 100-block chain:
    coinbaseKey.MakeNewKey(true);
    CScript scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    for (int i = 0; i < COINBASE_MATURITY; i++) {
        std::vector<CMutableTransaction> noTxns;
        CBlock b = CreateAndProcessBlock(noTxns, scriptPubKey);
        m_coinbase_txns.push_back(b.vtx[0]);
    }
}

CBlock TestChain100Setup::CreateAndProcessBlock(const std::vector<CMutableTransaction>& txns, const CScript& scriptPubKey)
{
    const CChainParams& chainparams = Params();
    CTxMemPool empty_pool;
    CBlock block = BlockAssembler(empty_pool, chainparams).CreateNewBlock(scriptPubKey)->block;

    Assert(block.vtx.size() == 1);
    for (const CMutableTransaction& tx : txns) {
        block.vtx.push_back(MakeTransactionRef(tx));
    }
    RegenerateCommitments(block);

    while (!CheckProofOfWork(block.GetHash(), block.nBits, chainparams.GetConsensus())) ++block.nNonce;

    std::shared_ptr<const CBlock> shared_pblock = std::make_shared<const CBlock>(block);
    Assert(m_node.chainman)->ProcessNewBlock(chainparams, shared_pblock, true, nullptr);

    return block;
}

TestChain100Setup::~TestChain100Setup()
{
    gArgs.ForceSetArg("-segwitheight", "0");
}

CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(const CMutableTransaction& tx)
{
    return FromTx(MakeTransactionRef(tx));
}

CTxMemPoolEntry TestMemPoolEntryHelper::FromTx(const CTransactionRef& tx)
{
    return CTxMemPoolEntry(tx, nFee, nTime, nHeight,
                           spendsCoinbase, sigOpCost, lp);
}

/**
 * @returns a real block (0000000000000a482a11624608cfd526e34036ca80707ee2a2b610c8b3e362a7)
 *      with 2 txs.
 */
CBlock getBlock6548()
{
    CBlock block;
    CDataStream stream(ParseHex("000000209d6d912905b56b0ea8d2515077f790c7074fd971733225671e0e000000000000d317bd76fd1aee3e3d393faf3e2f81ac9b432416eae1be1830cf2332722f11e8f46b695fe40e101ab0fa225502010000000001010000000000000000000000000000000000000000000000000000000000000000ffffffff4d02486504f46b695f08fabe6d6d0000000000000000000000000000000000000000000000000000000000000000010000000000000008000a8ae3e42c040f2f4d696e696e672d4475746368322f00000000020000000000000000266a24aa21a9ed4992791488fe02ca54d9be5c092716704d5451cd944c63698a59897df15c979d47c817a804000000160014923ae3df6b46c669e375f6389339adce9db0df6e012000000000000000000000000000000000000000000000000000000000000000000000000002000000000109300f98c44b09af6e05ebf4879a5df818ad7f266cfe340db89f5a50642730d68a0000000000feffffff4b607e4fe1170bc3834d76e44b4a92b3627efbc16212eb8c462e871332f6a3930100000000feffffff0bef2b10056001039400d76923a3fb52316b5bf988fe32ef8b9d9cfb7c07eb1d0100000000feffffffc4eb1dd6c58519a15997f1edeedcbd4dbb0eee55311ac0b554c6ce1cfc0a23130100000000feffffff9a9d602c838edd30e33eb5b18bcbff445ccf8fb98b0240ff5e1efbbac0434a3d0100000000feffffff5db403afe93a85a2db9dea5f2b116dd160f4bfbec922436a842b6b42c750f6390100000000feffffffdcbd24a5d2b25932c0e15d3b423cbd6caaa62c3f0c4e4d41cfa17ed0181582700100000000feffffff3ba08d8cbc68b99c789b110a5824fb7486d3b3fc9a4651571c134f30a1ed02620100000000feffffff16de1080de18442b8ef884e29a34490245d0ada4768bd11bb2f4b1a1f4d765060100000000feffffff035851d86001000000160014fa06400817ff97b6a22f3b57b794cd14765617c2674db641020000001600144e2c560419447349ebf04430f18cc3658a89fa6550888b8b24000000160014f253f5d00324795cda37d05711b8d56a430958b50247304402207c09390689738e4450ec91169dad23856f6f9e5742ae189f54126d252375e63202202d8417c783bb7f5ab5c5643547c22e95067eea9c253298593ce5244ff4a7fb1d0121025f25401531be284adf5588d027090eaf0c222e0f4e76609f695f8f5669438532024730440220502fa2f9e54ff919b23b2e5a68b15717d3ee8be0598853051fc0123c002e4f1c02207d84a0b3806242f2057560177a3fd42217c0d0ae3ae0407a49e06a89e4550e87012103bf2eac9e39f3fb03a62abd0566619f1bb159c50d86416655f090c7afa1ba18790247304402200849504d922bc7ddf530fa27febec8f178435a1b3d080ba848a0c03b7dc56063022079002a563e047ff4f463a2d9c587482a0f329893199ea71d82d11aaf0c767cf4012103bf2eac9e39f3fb03a62abd0566619f1bb159c50d86416655f090c7afa1ba18790247304402207888987f10cc304272510b2fbdafbf25cd99482784749f4363def16575b026cb02206dfac2a8d60838049171e1cd059dd2309f53bfbd9654f09dfc355496ed791116012103bf2eac9e39f3fb03a62abd0566619f1bb159c50d86416655f090c7afa1ba187902473044022013b2f364295e0df2a89187c37cfd9d99065a27c6835058561958650ef7b5c359022060aec9f74a72ad28c90406b84473631df8fd9bbd63c8f89d39a253000c9fa2b4012103bf2eac9e39f3fb03a62abd0566619f1bb159c50d86416655f090c7afa1ba18790247304402203d24485fe5edbb27fc264dac6fa6225bfaaff68afe3d62bcb81e8fb039ce20db02201033118c836f7c064f2b1b47ebf7322b75c61aaf37436f4c9832d6c47ce313bf012103bf2eac9e39f3fb03a62abd0566619f1bb159c50d86416655f090c7afa1ba18790247304402203110152d759a4a9d8924a70642f7fbdda11929771c30e68ab62435fe6a1abf0702201a269e8c1863f645ba2f78b0736e6e556222e75ac657b80ed977e239bdd511fc012103bf2eac9e39f3fb03a62abd0566619f1bb159c50d86416655f090c7afa1ba18790247304402203dad9123f2c407880121437a72e9ac240ce3c30e820016667686d88f15d4c9a802206e1b562286128e26227da4ad2898aca25f694e5eb1194e8c8a5e17fb4a87af65012103bf2eac9e39f3fb03a62abd0566619f1bb159c50d86416655f090c7afa1ba187902473044022057121c212513359e695463103c91691cf8aa4997d71611a839e426b8e74c606a022047003449d84457f690855e9807376d880551596fb1f8dfc38fe1b00a6a0a78af012103bf2eac9e39f3fb03a62abd0566619f1bb159c50d86416655f090c7afa1ba187947650000"), SER_NETWORK, PROTOCOL_VERSION);
    stream >> block;
    return block;
}
