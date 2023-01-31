#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test the RPC HTTP CORS."""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, str_to_b64str

import http.client
import urllib.parse

class HTTPCorsTest (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.cors_origin = "http://localhost:8000"
        self.extra_args = [["-rpcallowcors=" + self.cors_origin]]

    def run_test(self):

        url = urllib.parse.urlparse(self.nodes[0].url)
        authpair = url.username + ':' + url.password

        #same should be if we add keep-alive because this should be the std. behaviour
        headers = {"Authorization": "Basic " + str_to_b64str(authpair), "Connection": "keep-alive"}

        conn = http.client.HTTPConnection(url.hostname, url.port)
        conn.connect()
        conn.request('POST', '/', '{"method": "getbestblockhash"}', headers)
        res = conn.getresponse()
        self.check_cors_headers(res)
        assert_equal(res.status, http.client.OK)
        res.close()


        conn.request('OPTIONS', '/', '{"method": "getbestblockhash"}', headers)
        res = conn.getresponse()
        self.check_cors_headers(res)
        assert_equal(res.status, http.client.NO_CONTENT)
        res.close()

    def check_cors_headers(self, res):
        assert_equal(res.getheader("Access-Control-Allow-Origin"), self.cors_origin)
        assert_equal(res.getheader("Access-Control-Allow-Credentials"), "true")
        assert_equal(res.getheader("Access-Control-Allow-Methods"), "POST, GET, OPTIONS")
        assert_equal(res.getheader("Access-Control-Allow-Headers"), "Content-Type, Authorization")


if __name__ == '__main__':
    HTTPCorsTest ().main ()
