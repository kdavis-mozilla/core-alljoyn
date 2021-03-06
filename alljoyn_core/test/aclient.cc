/******************************************************************************
 * Copyright AllSeen Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/
#include <qcc/Thread.h>
#include <alljoyn/AboutData.h>
#include <alljoyn/AboutListener.h>
#include <alljoyn/AboutObjectDescription.h>
#include <alljoyn/AboutProxy.h>
#include <alljoyn/BusAttachment.h>
#include <alljoyn/Init.h>
#include <alljoyn/Session.h>
#include <alljoyn/SessionListener.h>

#include <signal.h>
#include <stdio.h>

using namespace ajn;
using namespace qcc;

static volatile sig_atomic_t s_interrupt = false;
static TransportMask s_transportMask = TRANSPORT_ANY;

static void CDECL_CALL SigIntHandler(int sig) {
    QCC_UNUSED(sig);
    s_interrupt = true;
}

BusAttachment* g_bus;

// Print out the fields found in the AboutData. Only fields with known signatures
// are printed out.  All others will be treated as an unknown field.
void printAboutData(AboutData& aboutData, const char* language, int tabNum)
{
    size_t count = aboutData.GetFields();
    // The 8 fields set above + 3 implicitly created fields: AJSoftwareVersion,
    // SupportedLanguages, and DefaultLanguage.

    const char** fields = new const char*[count];
    aboutData.GetFields(fields, count);

    for (size_t i = 0; i < count; ++i) {
        for (int j = 0; j < tabNum; ++j) {
            printf("\t");
        }
        printf("Key: %s", fields[i]);

        MsgArg* tmp;
        aboutData.GetField(fields[i], tmp, language);
        printf("\t");
        if (tmp->Signature() == "s") {
            const char* tmp_s;
            tmp->Get("s", &tmp_s);
            printf("%s", tmp_s);
        } else if (tmp->Signature() == "as") {
            size_t las;
            MsgArg* as_arg;
            tmp->Get("as", &las, &as_arg);
            for (size_t j = 0; j < las; ++j) {
                const char* tmp_s;
                as_arg[j].Get("s", &tmp_s);
                printf("%s ", tmp_s);
            }
        } else if (tmp->Signature() == "ay") {
            size_t lay;
            uint8_t* pay;
            tmp->Get("ay", &lay, &pay);
            // If the AboutData item being printed is AppId and it is
            // 128 bits = 16 octets in length, display it in UUID format.
            bool displayAppIDInUUIDFmt = ((0 == strcmp(fields[i], ajn::AboutKeys::APP_ID)) && (16 == lay));
            for (size_t j = 0; j < lay; ++j) {
                printf("%02x", pay[j]);
                // Hyphens are displayed after 4, 6, 8 and 10 octets
                // when printing UUID.
                if (!displayAppIDInUUIDFmt) {
                    printf(" ");
                } else if ((3 == j) || (5 == j) || (7 == j) || (9 == j)) {
                    printf("-");
                }
            }
        } else {
            printf("User Defined Value\tSignature: %s", tmp->Signature().c_str());
        }
        printf("\n");
    }
    delete [] fields;
}

class AboutThread : public Thread, public ThreadListener {
  public:
    static AboutThread* Launch(const char* busName, SessionPort port)
    {
        AboutThread* bgThread = new AboutThread(busName, port);
        bgThread->Start();

        return bgThread;
    }

    AboutThread(const char* busName, SessionPort port)
        : sender(busName), sessionPort(port) { }

    void ThreadExit(Thread* thread)
    {
        printf("Thread exit...\n");

        thread->Join();
        delete thread;
    }

  protected:

    ThreadReturn STDCALL Run(void* args)
    {
        QCC_UNUSED(args);
        QStatus status = ER_OK;

        SessionListener sessionListener;
        SessionId sessionId;
        SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, false, SessionOpts::PROXIMITY_ANY, s_transportMask);

        printf("Sender%s\n", sender);

        status = g_bus->JoinSession(sender, sessionPort, &sessionListener, sessionId, opts);

        if (ER_OK == status) {
            AboutProxy aboutProxy(*g_bus, sender, sessionId);

            MsgArg objArg;
            status = aboutProxy.GetObjectDescription(objArg);

            if (status == ER_OK) {
                printf("*********************************************************************************\n");
                printf("AboutProxy.GetObjectDescription:\n");
                AboutObjectDescription aod(objArg);
                size_t path_num = aod.GetPaths(NULL, 0);
                const char** paths = new const char*[path_num];
                aod.GetPaths(paths, path_num);
                for (size_t i = 0; i < path_num; ++i) {
                    printf("\t%s\n", paths[i]);
                    size_t intf_num = aod.GetInterfaces(paths[i], NULL, 0);
                    const char** intfs = new const char*[intf_num];
                    aod.GetInterfaces(paths[i], intfs, intf_num);
                    for (size_t j = 0; j < intf_num; ++j) {
                        printf("\t\t%s\n", intfs[j]);
                    }
                    delete [] intfs;
                }
                delete [] paths;
                printf("*********************************************************************************\n");

                MsgArg aArg;
                status = aboutProxy.GetAboutData(NULL, aArg);
                if (status == ER_OK) {
                    printf("*********************************************************************************\n");
                    printf("AboutProxy.GetAboutData: (Default Language)\n");
                    AboutData aboutData(aArg);
                    printAboutData(aboutData, NULL, 1);
                    size_t lang_num;
                    lang_num = aboutData.GetSupportedLanguages();
                    // If the lang_num == 1 we only have a default language
                    if (lang_num > 1) {
                        const char** langs = new const char*[lang_num];
                        aboutData.GetSupportedLanguages(langs, lang_num);
                        char* defaultLanguage;
                        aboutData.GetDefaultLanguage(&defaultLanguage);
                        // print out the AboutData for every language but the
                        // default it has already been printed.
                        for (size_t i = 0; i < lang_num; ++i) {
                            if (strcmp(defaultLanguage, langs[i]) != 0) {
                                status = aboutProxy.GetAboutData(langs[i], aArg);
                                if (ER_OK == status) {
                                    aboutData.CreatefromMsgArg(aArg, langs[i]);
                                    printf("AboutProxy.GetAboutData: (%s)\n", langs[i]);
                                    printAboutData(aboutData, langs[i], 1);
                                }
                            }
                        }
                        delete [] langs;
                    }
                    printf("*********************************************************************************\n");

                    uint16_t ver;
                    status = aboutProxy.GetVersion(ver);

                    if (status == ER_OK) {
                        printf("*********************************************************************************\n");
                        printf("AboutProxy.GetVersion %hd\n", ver);
                        printf("*********************************************************************************\n");
                    } else {
                        printf("AboutProxy.GetVersion failed(%s)\n", QCC_StatusText(status));
                    }
                } else {
                    printf("AboutProxy.GetAboutData failed(%s)\n", QCC_StatusText(status));
                }
            } else {
                printf("AboutProxy.GetObjectDescription failed(%s)\n", QCC_StatusText(status));
            }

            g_bus->LeaveSession(sessionId);

        } else {
            printf("JoinSession failed(%s)\n", QCC_StatusText(status));
        }
        return 0;
    }

  private:
    const char* sender;
    SessionPort sessionPort;

};

class MyAboutListener : public AboutListener {
    void Announced(const char* busName, uint16_t version, SessionPort port, const MsgArg& objectDescriptionArg, const MsgArg& aboutDataArg) {
        printf("*********************************************************************************\n");
        printf("Anounce signal discovered\n");
        printf("\tFrom bus %s\n", busName);
        printf("\tAbout version %hu\n", version);
        printf("\tSessionPort %hu\n", port);
        printf("\tAnnounced ObjectDescription:\n");
        AboutObjectDescription aod(objectDescriptionArg);
        size_t path_num = aod.GetPaths(NULL, 0);
        const char** paths = new const char*[path_num];
        aod.GetPaths(paths, path_num);
        for (size_t i = 0; i < path_num; ++i) {
            printf("\t\t%s\n", paths[i]);
            size_t intf_num = aod.GetInterfaces(paths[i], NULL, 0);
            const char** intfs = new const char*[intf_num];
            aod.GetInterfaces(paths[i], intfs, intf_num);
            for (size_t j = 0; j < intf_num; ++j) {
                printf("\t\t\t%s\n", intfs[j]);
            }
            delete [] intfs;
        }
        delete [] paths;

        printf("\tAnnounced AboutData:\n");
        AboutData aboutData(aboutDataArg);
        printAboutData(aboutData, NULL, 2);
        printf("*********************************************************************************\n");

        if (g_bus != NULL) {
            // Start a seperate thread to joinSession and GetAboutData
            AboutThread::Launch(::strdup(busName), port);
        } else {
            printf("BusAttachment is NULL\n");
        }
    }
};

static void usage(void)
{
    printf("Usage: aclient [-h] [-?] [-t] [-l] [-u] \n\n");
    printf("Options:\n");
    printf("   -h                    = Print this help message\n");
    printf("   -?                    = Print this help message\n");
    printf("   -t                    = Discover over TCP (enables selective discovering)\n");
    printf("   -l                    = Discover locally (enables selective discovering)\n");
    printf("   -u                    = Discover over UDP-based ARDP (enables selective discovering)\n");
    printf("\n");
}

int CDECL_CALL main(int argc, char** argv)
{

    /*Transport Flags*/
    for (int i = 1; i < argc; ++i) {
        if (0 == strcmp("-h", argv[i]) || 0 == strcmp("-?", argv[i])) {
            usage();
            exit(0);
        }  else if (0 == strcmp("-t", argv[i])) {
            s_transportMask = TRANSPORT_TCP;
        } else if (0 == strcmp("-l", argv[i])) {
            s_transportMask = TRANSPORT_LOCAL;
        } else if (0 == strcmp("-u", argv[i])) {
            s_transportMask = TRANSPORT_UDP;
        }
    }

    if (AllJoynInit() != ER_OK) {
        return 1;
    }
#ifdef ROUTER
    if (AllJoynRouterInit() != ER_OK) {
        AllJoynShutdown();
        return 1;
    }
#endif

    /* Install SIGINT handler so Ctrl + C deallocates memory properly */
    signal(SIGINT, SigIntHandler);

    QStatus status;

    g_bus = new BusAttachment("AboutServiceTest", true);

    status = g_bus->Start();
    if (ER_OK == status) {
        printf("BusAttachment started.\n");
    } else {
        printf("FAILED to start BusAttachment (%s)\n", QCC_StatusText(status));
        exit(1);
    }

    status = g_bus->Connect();
    if (ER_OK == status) {
        printf("BusAttachment connect succeeded. BusAttachment Unique name is %s\n", g_bus->GetUniqueName().c_str());
    } else {
        printf("FAILED to connect to router node (%s)\n", QCC_StatusText(status));
        exit(1);
    }

    MyAboutListener aboutListener;
    g_bus->RegisterAboutListener(aboutListener);

    const char* interfaces[] = { "org.alljoyn.About", "org.alljoyn.Icon" };
    status = g_bus->WhoImplements(interfaces, sizeof(interfaces) / sizeof(interfaces[0]));
    if (ER_OK == status) {
        printf("WhoImplements called.\n");
    } else {
        printf("WhoImplements call FAILED with status %s\n", QCC_StatusText(status));
        exit(1);
    }
    /* Perform the service asynchronously until the user signals for an exit. */
    if (ER_OK == status) {
        while (s_interrupt == false) {
            qcc::Sleep(100);
        }
    }

    g_bus->UnregisterAboutListener(aboutListener);
    delete g_bus;
#ifdef ROUTER
    AllJoynRouterShutdown();
#endif
    AllJoynShutdown();
    return 0;
}
