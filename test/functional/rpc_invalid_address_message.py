#!/usr/bin/env python3
# Copyright (c) 2020 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test error messages for 'getaddressinfo' and 'validateaddress' RPC commands."""

from test_framework.test_framework import BGLTestFramework

from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

BECH32_VALID = 'rbgl1qtmp74ayg7p24uslctssvjm06q5phz4yrlr4q2x'
BECH32_INVALID_BECH32 = 'rbgl1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqsjdr7p'
BECH32_INVALID_BECH32M = 'rbgl1qw508d6qejxtdg4y5r3zarvary0c5xw7kgtktm8'
BECH32_INVALID_VERSION = 'rbgl130xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqe68dw0'
BECH32_INVALID_SIZE = 'rbgl1s0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7v8n0nx0muaewav253wkc50'
BECH32_INVALID_V0_SIZE = 'rbgl1qw508d6qejxtdg4y5r3zarvary0c5xw7kqqdx75yt'
BECH32_INVALID_PREFIX = 'tbgl1pw508d6qejxtdg4y5r3zarvary0c5xw7kw508d6qejxtdg4y5r3zarvary0c5xw7khvqghp'

BASE58_VALID = 'MAMYWDWqd46sYwL7h9ExCpzaPba53HhMh8'
BASE58_INVALID_PREFIX = '17VZNX1SN5NtKa8UQFxwQbFeFc3iqRYhem'

INVALID_ADDRESS = 'asfah14i8fajz0123f'

class InvalidAddressErrorMessageTest(BGLTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def test_validateaddress(self):
        node = self.nodes[0]

        # Bech32
        info = node.validateaddress(BECH32_INVALID_SIZE)
        assert not info['isvalid']
        assert_equal(info['error'], 'Invalid Bech32 address data size')

        info = node.validateaddress(BECH32_INVALID_PREFIX)
        assert not info['isvalid']
        assert_equal(info['error'], 'Invalid prefix for Bech32 address')

        info = node.validateaddress(BECH32_INVALID_BECH32)
        assert not info['isvalid']
        assert_equal(info['error'], 'Version 1+ witness address must use Bech32m checksum')

        info = node.validateaddress(BECH32_INVALID_BECH32M)
        assert not info['isvalid']
        assert_equal(info['error'], 'Version 0 witness address must use Bech32 checksum')

        info = node.validateaddress(BECH32_INVALID_V0_SIZE)
        assert not info['isvalid']
        assert_equal(info['error'], 'Invalid Bech32 v0 address data size')

        info = node.validateaddress(BECH32_VALID)
        assert info['isvalid']
        assert 'error' not in info

        info = node.validateaddress(BECH32_INVALID_VERSION)
        assert not info['isvalid']
        assert_equal(info['error'], 'Invalid Bech32 address witness version')

        # Base58
        info = node.validateaddress(BASE58_INVALID_PREFIX)
        assert not info['isvalid']
        assert_equal(info['error'], 'Invalid prefix for Base58-encoded address')

        info = node.validateaddress(BASE58_VALID)
        assert info['isvalid']
        assert 'error' not in info

        # Invalid address format
        info = node.validateaddress(INVALID_ADDRESS)
        assert not info['isvalid']
        assert_equal(info['error'], 'Invalid address format')

    def test_getaddressinfo(self):
        node = self.nodes[0]

        assert_raises_rpc_error(-5, "Invalid Bech32 address data size", node.getaddressinfo, BECH32_INVALID_SIZE)

        assert_raises_rpc_error(-5, "Invalid prefix for Bech32 address", node.getaddressinfo, BECH32_INVALID_PREFIX)

        assert_raises_rpc_error(-5, "Invalid prefix for Base58-encoded address", node.getaddressinfo, BASE58_INVALID_PREFIX)

        assert_raises_rpc_error(-5, "Invalid address format", node.getaddressinfo, INVALID_ADDRESS)

    def run_test(self):
        self.test_validateaddress()

        if self.is_wallet_compiled():
            self.init_wallet(node=0)
            self.test_getaddressinfo()


if __name__ == '__main__':
    InvalidAddressErrorMessageTest().main()
