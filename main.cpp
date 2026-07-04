// Pure Haiku implementation of CopyPasted
//
// MIT License by lukolve
//
// pkgman install devel:libnetwork

#include <Application.h>
#include <Clipboard.h>
#include <Message.h>
#include <MessageFilter.h>
#include <SupportDefs.h>
#include <Url.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>
#include <File.h>
#include <stdio.h>
#include <string.h>

using namespace BPrivate::Network;

// Definicia vlastnej spravy pre nasu skratku
const uint32 MSG_ALT_PASTE = 'alps';

const char* SERVER_URL_GET = "http://192.168.1.100:5000/get_clipboard"; // Uprav IP
const char* SERVER_URL_CLEAR = "http://192.168.1.100:5000/clear_clipboard";

//#define SERVER_URL_GET "http://192.168.1.100:5000/get_clipboard" // Uprav IP
//#define SERVER_URL_CLEAR "http://192.168.1.100:5000/clear_clipboard"

// --- KROK 2 & 3: Filter klavesnice ---
filter_result KeyFilter(BMessage* message, BHandler** target, BMessageFilter* filter)
{
    int32 modifiers;
    int32 key;
    
    if (message->FindInt32("modifiers", &modifiers) == B_OK
        && message->FindInt32("key", &key) == B_OK) {
        
        // B_COMMAND_KEY je v Haiku standardne Alt (alebo Win klaves podla nastavenia)
        // Klaves 'v' ma na standardnej klavesnici vacsinou kod 0x5b alebo to overime cez b_key
        const char* bytes;
        if (message->FindString("bytes", &bytes) == B_OK && strcmp(bytes, "v") == 0) {
            if ((modifiers & B_COMMAND_KEY) != 0) {
                // Zachytili sme Alt+V, posleme spravu nasej aplikacii
                be_app->PostMessage(MSG_ALT_PASTE);
                return B_SKIP_MESSAGE; // Ignorujeme povodnu spravu, spracujeme ju sami
            }
        }
    }
    return B_DISPATCH_MESSAGE;
}

// We define our own Application class by inheriting from BApplication
class CopyPasteApp : public BApplication {
public:
    // Constructor: we give our app a unique signature
    CopyPasteApp() : BApplication("application/x-vnd.CopyPasteApp") {
        printf("CopyPasteApp started. Watching clipboard... Press Ctrl+C to exit.\n");
        
        // Pridame globalny filter na klavesnicu pre celu aplikaciu
        fFilter = new BMessageFilter(B_KEY_DOWN, KeyFilter);
        AddCommonFilter(fFilter);
        printf("CopyPasteApp client side app started. Press Alt+V...\n");
    }

~CopyPasteApp()
    {
        Lock();
        RemoveCommonFilter(fFilter);
        delete fFilter;
        Unlock();
    }


    // This method is called automatically right after the application starts
    void ReadyToRun() override {
        // Tell the system clipboard to send a message to our app whenever it changes
        be_clipboard->StartWatching(be_app_messenger);
    }

    // This is the core loop where the app receives and processes system events/messages
    void MessageReceived(BMessage* message) override {
        switch (message->what) {
            case B_CLIPBOARD_CHANGED: {
                printf("\n[!] Clipboard update detected!\n");
                
                // We must lock the clipboard before reading from it
                if (be_clipboard->Lock()) {
                    BMessage* clipData = be_clipboard->Data();
                    const char* text = nullptr;
                    ssize_t textLen = 0;

                    // Extract text data from the clipboard message
                    // "text/plain" is the standard MIME type for unformatted text
                    if (clipData->FindData("text/plain", B_MIME_TYPE, (const void**)&text, &textLen) == B_OK) {
                        // Create a safe null-terminated string to print
                        BString copiedText(text, textLen);
                        printf("Copied text: %s\n", copiedText.String());
                    } else {
                        printf("Clipboard changed, but it doesn't contain plain text.\n");
                    }

                    // Always unlock the clipboard when done!
                    be_clipboard->Unlock();
                }
                break;
            case MSG_ALT_PASTE:
                SpracujAltPaste();
                break;
            }
            default:
                // Pass any other unhandled messages to the base class
                BApplication::MessageReceived(message);
                break;
        }
    }
    
private:
    BMessageFilter* fFilter;
    
    void SpracujAltPaste()
    {
        printf("[INFO] I see there is pressed Alt+V. Connecting to server...\n");

        // 1. Stiahnutie dat z weboveho servera (Krok 2)
        BUrl url(SERVER_URL_GET,false);
        //BUrl url(urlString);
        BMallocIO replyBuffer; // Sem ulozime odpoved zo servera
        
        // V Haiku standardne pouzivame UrlProtocolRoster na sietove poziadavky
        BUrlRequest* request = BUrlProtocolRoster::MakeRequest(url, &replyBuffer);
        if (request == NULL) {
            printf("[ERROR] Nepodarilo sa vytvorit sietovy poziadavok.\n");
            return;
        }

        status_t thread = request->Run();
        wait_for_thread(thread, NULL);
        delete request;

        // Predpokladajme, ze server zatial vracia len cisty text (nie JSON), aby sme si to v C++ nekomplikovali
        // Ak vracia JSON {"obsah": "text"}, museli by sme pouzit BMessage s JSON prekladacom, 
        // takze pre test BeAPI staci, ak tvoj server vrati iba cisty text ako string.
        
        size_t dataSize = replyBuffer.BufferLength();
        if (dataSize == 0) {
            printf("[INFO] Server gets clear message.\n");
            return;
        }

        char* stiahnutyText = (char*)malloc(dataSize + 1);
        memcpy(stiahnutyText, replyBuffer.Buffer(), dataSize);
        stiahnutyText[dataSize] = '\0';

        printf("[INFO] Downloaded message: %s\n", stiahnutyText);

        // 2. Vlozenie do systemoveho BeAPI Clipboardu
        if (be_clipboard->Lock()) {
            be_clipboard->Clear();
            
            BMessage* clipData = be_clipboard->Data();
            if (clipData != NULL) {
                // BeAPI pouziva na text typ "text/plain"
                clipData->AddData("text/plain", B_MIME_TYPE, stiahnutyText, dataSize);
                be_clipboard->Commit();
                printf("[INFO] Data writed to BeOS clipboard.\n");
            }
            be_clipboard->Unlock();
        }
        
        free(stiahnutyText);

        // TODO: Tu by nasledovala simulacia systemoveho "Paste" do aktualneho okna.
        // V Haiku sa to robi poslanim B_PASTE spravy aktivnemu oknu (view),
        // ale kedze sme skratku zachytili, pouzivatel moze hned stlacit standardne Paste,
        // alebo docielime auto-paste. Pre test staci, ze to uz mas v clipboarde.

        // 3. Krok 3: Poslanie poziadavky na uvolnenie serveru (POST alebo GET na /clear)
        printf("[INFO] Posielam poziadavok na uvolnenie serveru...\n");
        
        //BString clearUrlString
        BUrl clearUrl(SERVER_URL_CLEAR,false);
        //BUrl clearUrl(clearUrlString);
        BMallocIO clearReply;
        BUrlRequest* clearRequest = BUrlProtocolRoster::MakeRequest(clearUrl, &clearReply);
        if (clearRequest != NULL) {
            status_t cThread = clearRequest->Run();
            wait_for_thread(cThread, NULL);
            delete clearRequest;
            printf("[SUCCESS] Server gets clear request.\n");
        }
    }
};

int main() {
    // Create an instance of our application and run its main loop
    CopyPasteApp app;
    app.Run();
    return 0;
}
