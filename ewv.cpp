#include <iostream>
#include <string>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <cstdlib>

using namespace std;

const string EWV_VERSION = "1.2.0-STABLE";

void print_help() {
    cout << "\n--- EWV Launcher (ThunderOS) ---\n"
         << "Usage: ewv [COMMAND] [WIDGET_NAME]\n\n"
         << "Commands:\n"
         << "  run [name]   - Start a widget (auto-detects .yuck, .xml, .conf)\n"
         << "  kill [name]  - Stop a specific running widget\n"
         << "  help         - Display this help menu\n"
         << "  -v           - Display version information\n"
         << "-----------------------------------\n";
}

bool file_exists(const string& name) {
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

string get_pid_file(string widget) {
    return "/tmp/ewv_" + widget + ".pid";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { print_help(); return 1; }

    string cmd = argv[1];
    if (cmd == "-v" || cmd == "--version") {
        cout << "ewv " << EWV_VERSION << "\nCredits: mattia225 on GitHub\n";
        return 0;
    }
    if (cmd == "help") { print_help(); return 0; }
    if (argc < 3) { cerr << "Error: Missing widget name.\n"; return 1; }

    string widget = argv[2];
    const char* user = getenv("USER");
    if (!user) { cerr << "Error: Could not determine user.\n"; return 1; }

    string config_dir = "/home/" + string(user) + "/.config/ewv/" + widget + "/";
    string pid_path = get_pid_file(widget);

    // --- COMMAND: RUN ---
    if (cmd == "run") {
        if (file_exists(pid_path)) {
            // Logica PID invariata
            cerr << "Error: Widget '" << widget << "' is already running.\n";
            return 1;
        }

        // --- IL CUORE DELLA MODIFICA: PERCORSI DINAMICI ---
        string internal_lib = "/usr/local/lib/ewv/";
        string provider;

        if (file_exists(config_dir + "config.yuck")) {
            provider = internal_lib + "ewv-yuck-provider";
            cout << "[*] Detecting .yuck format for: " << widget << "\n";
        } else if (file_exists(config_dir + "config.xml")) {
            provider = internal_lib + "ewv-xml-provider";
            cout << "[*] Detecting .xml format for: " << widget << "\n";
        } else {
            provider = internal_lib + "ewv-conf-provider";
            cout << "[*] Detecting standard .conf format for: " << widget << "\n";
        }

        setenv("LD_PRELOAD", "/usr/lib/libgtk4-layer-shell.so", 1);
        setenv("GSK_RENDERER", "gl", 1);
        setenv("GTK_USE_PORTAL", "0", 1);

        pid_t pid = fork();
        if (pid == 0) {
            freopen("/dev/null", "w", stderr);
            execl(provider.c_str(), provider.c_str(), widget.c_str(), config_dir.c_str(), NULL);
            perror("Fatal: Failed to execute provider");
            exit(1);
        } else if (pid > 0) {
            ofstream pid_file(pid_path);
            pid_file << pid;
            cout << "[+] Widget '" << widget << "' started successfully (PID: " << pid << ")\n";
        }
    } 
    // --- COMMAND: KILL (Invariato) ---
    else if (cmd == "kill") {
        ifstream pid_file(pid_path);
        if (pid_file.is_open()) {
            pid_t pid; pid_file >> pid;
            cout << "[-] Terminating widget '" << widget << "' (PID " << pid << ")...\n";
            kill(pid, SIGTERM);
            remove(pid_path.c_str());
        } else {
            cerr << "Error: Widget '" << widget << "' is not running.\n";
        }
    }
    return 0;
}
