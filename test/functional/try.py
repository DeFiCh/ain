from test_framework.test_framework import DefiTestFramework
import calendar
import time
from decimal import Decimal

class Verify (DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-txnotokens=0', '-amkheight=5', '-bayfrontheight=5', '-eunosheight=10', '-fortcanningheight=11', '-fortcanninghillheight=12', '-fortcanningroadheight=13', '-fortcanningcrunchheight=14', '-fortcanningspringheight=15', '-grandcentralheight=16']]

    def run_test(self):
        node = self.nodes[0]

        node.generate(200)

        account0 = node.get_genesis_keys().ownerAuthAddress
        symbolBTC = "BTC"
        symbolTSLA = "TSLA"
        symbolDFI = "DFI"
        symboldUSD = "DUSD"


        self.nodes[0].createtoken({
            "symbol": symbolBTC,
            "name": "BTC token",
            "isDAT": True,
            "collateralAddress": account0
        })
        self.nodes[0].createtoken({
            "symbol": symboldUSD,
            "name": "DUSD token",
            "isDAT": True,
            "collateralAddress": account0
        })
        self.nodes[0].generate(1)

        oracle_address1 = self.nodes[0].getnewaddress("", "legacy")
        price_feeds1 = [{"currency": "USD", "token": "DFI"},
                        {"currency": "USD", "token": "BTC"},
                        {"currency": "USD", "token": "TSLA"},
                        {"currency": "USD", "token": "GOOGL"}]
        oracle_id1 = self.nodes[0].appointoracle(oracle_address1, price_feeds1, 10)
        self.nodes[0].generate(1)
        oracle1_prices = [{"currency": "USD", "tokenAmount": "10@TSLA"},
                          {"currency": "USD", "tokenAmount": "10@GOOGL"},
                          {"currency": "USD", "tokenAmount": "10@DFI"},
                          {"currency": "USD", "tokenAmount": "10@BTC"}]
        timestamp = calendar.timegm(time.gmtime())
        self.nodes[0].setoracledata(oracle_id1, timestamp, oracle1_prices)
        self.nodes[0].generate(1)

        idBTC = list(self.nodes[0].gettoken(symbolBTC).keys())[0]
        idDFI = list(self.nodes[0].gettoken(symbolDFI).keys())[0]
        iddUSD = list(self.nodes[0].gettoken(symboldUSD).keys())[0]
        self.nodes[0].setcollateraltoken({
            'token': idBTC,
            'factor': 1,
            'fixedIntervalPriceId': "BTC/USD"})
        self.nodes[0].setcollateraltoken({
            'token': idDFI,
            'factor': 1,
            'fixedIntervalPriceId': "DFI/USD"})
        self.nodes[0].generate(1)

        setLoanTokenTSLA = self.nodes[0].setloantoken({
            'symbol': symbolTSLA,
            'name': "Tesla stock token",
            'fixedIntervalPriceId': "TSLA/USD",
            'mintable': True,
            'interest': 1})
        self.nodes[0].createloanscheme(150, 5, 'LOAN150')
        self.nodes[0].generate(10)
        idTSLA = list(self.nodes[0].getloantoken(symbolTSLA)["token"])[0]

        vaultId = self.nodes[0].createvault(account0, 'LOAN150')
        self.nodes[0].minttokens(["2@" + symbolBTC])
        node.utxostoaccount({account0: "2000@DFI"})
        self.nodes[0].generate(1)

        self.nodes[0].deposittovault(vaultId, account0, "2000@DFI")
        self.nodes[0].generate(1)

        poolOwner = self.nodes[0].getnewaddress("", "legacy")

        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idTSLA,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-TSLA",
        }, [])
        self.nodes[0].createpoolpair({
            "tokenA": iddUSD,
            "tokenB": idDFI,
            "commission": Decimal('0.002'),
            "status": True,
            "ownerAddress": poolOwner,
            "pairSymbol": "DUSD-DFI",
        }, [])
        node.generate(1)

        self.nodes[0].minttokens(["4000@" + symboldUSD, "2000@" + symbolTSLA])
        node.utxostoaccount({account0: "100@DFI"})
        node.generate(1)

        node.addpoolliquidity({
            account0: ["2000@" + symboldUSD, "100@" + symbolDFI]
        }, account0, [])
        node.addpoolliquidity({
            account0: ["2000@" + symboldUSD, "2000@" + symbolTSLA]
        }, account0, [])
        node.generate(1)

        self.nodes[0].takeloan({
            'vaultId': vaultId,
            'amounts': "1@" + symbolTSLA})
        self.nodes[0].generate(1)

        self.nodes[0].paybackloan({
            'vaultId': vaultId,
            'from': account0,
            'amounts': "0.5@" + symbolTSLA})
        self.nodes[0].generate(1)

        self.nodes[0].paybackloan({
            'vaultId': vaultId,
            'from': account0,
            'amounts': "0.5@" + symbolTSLA})
        self.nodes[0].generate(1)

        # self.nodes[0].generate(6)

        preinvalid = node.getburninfo()
        blockhash = node.getblockhash(node.getblockcount() - 5)
        node.invalidateblock(blockhash)
        burninfo = node.getburninfo()
        print(node.verifychain())

        self.restart_node(0)

        print(node.verifychain())
        assert(node.getburninfo() == burninfo)

        node.reconsiderblock(blockhash)
        assert(node.getburninfo() == preinvalid)

        raise Exception()

if __name__ == '__main__':
    Verify().main()
