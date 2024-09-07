// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/defi-config.h>
#endif

#include <chainparams.h>
#include <clientversion.h>
#include <compat.h>
#include <fs.h>
#include <init.h>
#include <interfaces/chain.h>
#include <noui.h>
#include <shutdown.h>
#include <ui_interface.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <util/threadnames.h>
#include <util/tokenpipe.h>
#include <util/translation.h>
#include <ain_rs_exports.h>
#include <ffi/ffihelpers.h>
#include <functional>

const std::function<std::string(const char*)> G_TRANSLATION_FUN = nullptr;

#if HAVE_DECL_FORK

/** Custom implementation of daemon(). This implements the same order of operations as glibc.
 * Opens a pipe to the child process to be able to wait for an event to occur.
 *
 * @returns 0 if successful, and in child process.
 *          >0 if successful, and in parent process.
 *          -1 in case of error (in parent process).
 *
 *          In case of success, endpoint will be one end of a pipe from the child to parent process,
 *          which can be used with TokenWrite (in the child) or TokenRead (in the parent).
 */
int fork_daemon(bool nochdir, bool noclose, TokenPipeEnd& endpoint)
{
    // communication pipe with child process
    std::optional<TokenPipe> umbilical = TokenPipe::Make();
    if (!umbilical) {
        return -1; // pipe or pipe2 failed.
    }

    int pid = fork();
    if (pid < 0) {
        return -1; // fork failed.
    }
    if (pid != 0) {
        // Parent process gets read end, closes write end.
        endpoint = umbilical->TakeReadEnd();
        umbilical->TakeWriteEnd().Close();

        int status = endpoint.TokenRead();
        if (status != 0) { // Something went wrong while setting up child process.
            endpoint.Close();
            return -1;
        }

        return pid;
    }
    // Child process gets write end, closes read end.
    endpoint = umbilical->TakeWriteEnd();
    umbilical->TakeReadEnd().Close();

#if HAVE_DECL_SETSID
    if (setsid() < 0) {
        exit(1); // setsid failed.
    }
#endif

    if (!nochdir) {
        if (chdir("/") != 0) {
            exit(1); // chdir failed.
        }
    }
    if (!noclose) {
        // Open /dev/null, and clone it into STDIN, STDOUT and STDERR to detach
        // from terminal.
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            bool err = dup2(fd, STDIN_FILENO) < 0 || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0;
            // Don't close if fd<=2 to try to handle the case where the program was invoked without any file descriptors open.
            if (fd > 2) close(fd);
            if (err) {
                exit(1); // dup2 failed.
            }
        } else {
            exit(1); // open /dev/null failed.
        }
    }
    endpoint.TokenWrite(0); // Success
    return 0;
}

#endif

/* Introduction text for doxygen: */

/*! \mainpage Developer documentation
 *
 * \section intro_sec Introduction
 *
 * This is the developer documentation of the reference client for an experimental new digital currency called Defi,
 * which enables instant payments to anyone, anywhere in the world. Defi uses peer-to-peer technology to operate
 * with no central authority: managing transactions and issuing money are carried out collectively by the network.
 *
 * The software is a community-driven open source project, released under the MIT license.
 *
 * See https://github.com/bitcoin/bitcoin and https://bitcoincore.org/ for further information about the project.
 *
 * \section Navigation
 * Use the buttons <code>Namespaces</code>, <code>Classes</code> or <code>Files</code> at the top of the page to start navigating the code.
 */

static void WaitForShutdown()
{
    while (!ShutdownRequested())
    {
        UninterruptibleSleep(std::chrono::milliseconds{200});
    }
    Interrupt();
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
static bool AppInit(int argc, char* argv[])
{
    XResultThrowOnErr(ain_rs_preinit(result));

    InitInterfaces interfaces;
    interfaces.chain = interfaces::MakeChain();

    bool fRet = false;

    util::ThreadRename("init");

    //
    // Parameters
    //
    // If Qt is used, parameters/defi.conf are parsed in qt/defi.cpp's main()
    SetupServerArgs();
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        return InitError(strprintf("Error parsing command line arguments: %s\n", error));
    }

    // Process help and version before taking care about datadir
    if (HelpRequested(gArgs) || gArgs.IsArgSet("-version")) {
        std::string strUsage = PACKAGE_NAME " Daemon version " + FormatVersionAndSuffix() + "\n";

        if (gArgs.IsArgSet("-version"))
        {
            strUsage += FormatParagraph(LicenseInfo()) + "\n";
        }
        else
        {
            strUsage += "\nUsage:  defid [options]                     Start " PACKAGE_NAME " Daemon\n";
            strUsage += "\n" + gArgs.GetHelpMessage();
        }

        tfm::format(std::cout, "%s", strUsage.c_str());
        return true;
    }

    #if HAVE_DECL_FORK
        // Communication with parent after daemonizing. This is used for signalling in the following ways:
        // - a boolean token is sent when the initialization process (all the Init* functions) have finished to indicate
        // that the parent process can quit, and whether it was successful/unsuccessful.
        // - an unexpected shutdown of the child process creates an unexpected end of stream at the parent
        // end, which is interpreted as failure to start.
        TokenPipeEnd daemon_ep;
    #endif
    try
    {
        if (!CheckDataDirOption()) {
            return InitError(strprintf("Specified data directory \"%s\" does not exist.\n", gArgs.GetArg("-datadir", "")));
        }
        if (!gArgs.ReadConfigFiles(error, true)) {
            return InitError(strprintf("Error reading configuration file: %s\n", error));
        }
        // Check for -testnet, -changi or -regtest parameter (Params() calls are only valid after this clause)
        try {
            SelectParams(gArgs.GetChainName());
        } catch (const std::exception& e) {
            return InitError(strprintf("%s\n", e.what()));
        }

        // Error out when loose non-argument tokens are encountered on command line
        for (int i = 1; i < argc; i++) {
            if (!IsSwitchChar(argv[i][0])) {
                return InitError(strprintf("Command line contains unexpected token '%s', see defid -h for a list of options.\n", argv[i]));
            }
        }

        // -server defaults to true for defid but not for the GUI so do this here
        gArgs.SoftSetBoolArg("-server", true);
        // Set this early so that parameter interactions go to console
        InitLogging();

        auto res = XResultStatusLogged(ain_rs_init_logging(result));
        if (!res) return false;

        InitParameterInteraction();
        if (!AppInitBasicSetup())
        {
            // InitError will have been called with detailed error, which ends up on console
            return false;
        }
        if (!AppInitParameterInteraction())
        {
            // InitError will have been called with detailed error, which ends up on console
            return false;
        }
        if (!AppInitSanityChecks())
        {
            // InitError will have been called with detailed error, which ends up on console
            return false;
        }
        if (gArgs.GetBoolArg("-daemon", false) || gArgs.GetBoolArg("-daemonwait", DEFAULT_DAEMONWAIT))
        {
#if HAVE_DECL_FORK
            tfm::format(std::cout, PACKAGE_NAME " starting\n");

            // Daemonize
            switch (fork_daemon(1, 0, daemon_ep)) { // don't chdir (1), do close FDs (0)
            case 0: // Child: continue.
                // If -daemonwait is not enabled, immediately send a success token the parent.
                if (!gArgs.GetBoolArg("-daemonwait", DEFAULT_DAEMONWAIT)) {
                    daemon_ep.TokenWrite(1);
                    daemon_ep.Close();
                }
                break;
            case -1: // Error happened.
                return InitError(strprintf("fork_daemon() failed: %s\n", strerror(errno)));
            default: { // Parent: wait and exit.
                int token = daemon_ep.TokenRead();
                if (token) { // Success
                    exit(EXIT_SUCCESS);
                } else { // fRet = false or token read error (premature exit).
                    tfm::format(std::cerr, "Error during initializaton - check debug.log for details\n");
                    exit(EXIT_FAILURE);
                }
            }
            }
#else
            return InitError("-daemon is not supported on this operating system\n");
#endif // HAVE_DECL_FORK
        }
        // Lock data directory after daemonization
        if (!AppInitLockDataDirectory())
        {
            // If locking the data directory failed, exit immediately
            return false;
        }
        fRet = AppInitMain(interfaces);
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "AppInit()");
    } catch (...) {
        PrintExceptionContinue(nullptr, "AppInit()");
    }

#if HAVE_DECL_FORK
    if (daemon_ep.IsOpen()) {
        // Signal initialization status to parent, then close pipe.
        daemon_ep.TokenWrite(fRet);
        daemon_ep.Close();
    }
#endif
    if (!fRet)
    {
        Interrupt();
    } else {
        WaitForShutdown();
    }
    Shutdown(interfaces);

    return fRet;
}

int main(int argc, char* argv[])
{
#ifdef WIN32
    util::WinCmdLineArgs winArgs;
    std::tie(argc, argv) = winArgs.get();
#endif
    SetupEnvironment();

    // Connect defid signal handlers
    noui_connect();

    return (AppInit(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE);
}
