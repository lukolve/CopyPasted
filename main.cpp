#include <Application.h>
#include <Clipboard.h>
#include <Message.h>
#include <MessageFilter.h>
#include <String.h>
#include <Roster.h>
#include <stdio.h>
#include <string.h>

// Hlavičkové súbory pre BSD sokety a asynchrónne vlákna
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <thread>
#include <string>

// Konfigurácia tvojho C++ servera
const char* SERVER_IP = "192.168.0.100";
const int SERVER_PORT = 5000;

// Naša vlastná správa pre vyvolanie vloženia zo servera
const uint32 MSG_ALT_PASTE = 'alps';

// --- Globalny filter klavesnice ---
filter_result KeyFilter(BMessage* message, BHandler** target, BMessageFilter* filter)
{
    int32 modifiers;
    int32 key;
    
    if (message->FindInt32("modifiers", &modifiers) == B_OK
        && message->FindInt32("key", &key) == B_OK) {
        
        const char* bytes;
        if (message->FindString("bytes", &bytes) == B_OK && strcmp(bytes, "v") == 0) {
            // B_COMMAND_KEY je v Haiku standardne klaves Alt
            if ((modifiers & B_COMMAND_KEY) != 0) {
                if (filter && filter->Looper()) {
                    // Bezpečne pošleme správu loopru (aplikácii)
                    filter->Looper()->PostMessage(MSG_ALT_PASTE);
                }
                return B_SKIP_MESSAGE; // Ignorujeme povodnu spravu v systéme
            }
        }
    }
    return B_DISPATCH_MESSAGE;
}

// --- SÚČASŤ 1: Odoslanie lokálnej schránky na server (Ctrl+C / Copy) ---
void OdosliNaServerNaPozadi(BString textNaOdoslanie) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr) <= 0 ||
        connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(sock);
        return;
    }

    // Keďže píšeš vlastný server v C++, posielame minimalistický HTTP POST požiadavok.
    // Ak tvoj server nebude spracovávať HTTP, môžeš poslať priamo surový text.
    std::string request = "POST /set_clipboard HTTP/1.1\r\n";
    request += "Host: " + std::string(SERVER_IP) + "\r\n";
    request += "Content-Length: " + std::to_string(textNaOdoslanie.Length()) + "\r\n";
    request += "Connection: close\r\n\r\n";
    request += textNaOdoslanie.String();

    send(sock, request.c_str(), request.length(), 0);
    close(sock);
    printf("[Soket] Schránka bola odoslaná na server.\n");
}

// --- SÚČASŤ 2: Stiahnutie zo servera a vloženie (Alt+V / Paste) ---
void SpracujPasteNaPozadi() {
    printf("[Soket] Pripájanie k serveru pre stiahnutie dát...\n");

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr) <= 0 ||
        connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        printf("[Soket - ERROR] Server je nedostupný.\n");
        close(sock);
        return;
    }

    // Posielame HTTP GET na získanie schránky
    std::string request = "GET /get_clipboard HTTP/1.1\r\n";
    request += "Host: " + std::string(SERVER_IP) + "\r\n";
    request += "Connection: close\r\n\r\n";

    send(sock, request.c_str(), request.length(), 0);

    // Čítanie odpovede zo soketu
    char buffer[1024];
    std::string httpResponse = "";
    ssize_t bytesRead;
    while ((bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytesRead] = '\0';
        httpResponse += buffer;
    }
    close(sock);

    // Oddelenie HTTP hlavičiek od samotného tela textu
    std::string stiahnutyText = "";
    size_t bodyPos = httpResponse.find("\r\n\r\n");
    if (bodyPos != std::string::npos) {
        stiahnutyText = httpResponse.substr(bodyPos + 4);
    }

    if (stiahnutyText.empty()) {
        printf("[Soket - INFO] Server vrátil prázdny text.\n");
        return;
    }

    printf("[Soket - SUCCESS] Stiahnutý text: %s\n", stiahnutyText.c_str());

    // Vloženie stiahnutého textu do systémového clipboardu Haiku
    if (be_clipboard->Lock()) {
        be_clipboard->Clear();
        BMessage* clipData = be_clipboard->Data();
        if (clipData != NULL) {
            clipData->AddData("text/plain", B_MIME_TYPE, stiahnutyText.c_str(), stiahnutyText.length());
            be_clipboard->Commit();
            printf("[Soket] Dáta zapísané do Haiku schránky.\n");
        }
        be_clipboard->Unlock();
    }

    // Automatická simulácia stlačenia "Paste" (odoslanie správy aktívnej aplikácii)
    app_info info;
    if (be_roster->GetActiveAppInfo(&info) == B_OK) {
        BMessenger targetApp(info.signature);
        if (targetApp.IsValid()) {
            targetApp.SendMessage(B_PASTE);
            printf("[Soket] Odoslaná správa B_PASTE pre: %s\n", info.signature);
        }
    }

    // Spätná väzba serveru - poslanie požiadavky na vyčistenie (/clear_clipboard)
    int clearSock = socket(AF_INET, SOCK_STREAM, 0);
    if (clearSock >= 0) {
        if (connect(clearSock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) >= 0) {
            std::string clearRequest = "GET /clear_clipboard HTTP/1.1\r\n";
            clearRequest += "Host: " + std::string(SERVER_IP) + "\r\n";
            clearRequest += "Connection: close\r\n\r\n";
            send(clearSock, clearRequest.c_str(), clearRequest.length(), 0);
            while (recv(clearSock, buffer, sizeof(buffer) - 1, 0) > 0); // Vyprázdnenie buffra
        }
        close(clearSock);
    }
}

// --- Hlavná trieda aplikácie ---
class CopyPasteApp : public BApplication {
public:
    CopyPasteApp() : BApplication("application/x-vnd.CopyPasteApp") {
        printf("CopyPasteApp started.\n");
        printf("Watching local clipboard (Alt+C) and listening for Alt+V...\n");
        
        // Registrácia globálneho filtra klávesnice
        fFilter = new BMessageFilter(B_KEY_DOWN, KeyFilter);
        AddCommonFilter(fFilter);
    }

    ~CopyPasteApp() {
        Lock();
        RemoveCommonFilter(fFilter);
        delete fFilter;
        Unlock();
    }

    void ReadyToRun() override {
        // Sledovanie zmien lokálneho clipboardu
        be_clipboard->StartWatching(be_app_messenger);
    }

    void MessageReceived(BMessage* message) override {
        switch (message->what) {
            case B_CLIPBOARD_CHANGED: {
                printf("\n[!] Clipboard update detected!\n");
                
                if (be_clipboard->Lock()) {
                    BMessage* clipData = be_clipboard->Data();
                    const char* text = nullptr;
                    ssize_t textLen = 0;

                    if (clipData->FindData("text/plain", B_MIME_TYPE, (const void**)&text, &textLen) == B_OK) {
                        BString copiedText(text, textLen);
                        printf("Copied text: %s\n", copiedText.String());

                        // Asynchrónne odoslanie na server
                        std::thread(OdosliNaServerNaPozadi, copiedText).detach();
                    }
                    be_clipboard->Unlock();
                }
                break;
            }
            
            case MSG_ALT_PASTE: {
                // Používateľ stlačil skratku, asynchrónne sťahujeme zo servera
                std::thread(SpracujPasteNaPozadi).detach();
                break;
            }
            
            default:
                BApplication::MessageReceived(message);
                break;
        }
    }

private:
    BMessageFilter* fFilter;
};

int main() {
    CopyPasteApp app;
    app.Run();
    return 0;
}
