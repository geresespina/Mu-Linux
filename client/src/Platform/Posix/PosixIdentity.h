#pragma once
#include "Platform/IIdentity.h"
#include <cstdio>
#include <cstring>
namespace Platform {
class PosixIdentity final : public IIdentity {
    const char* primary;
    const char* alternate;
    static bool ReadFile(const char* path, char (&output)[36]) {
        FILE* file = std::fopen(path, "rb");
        if (!file) return false;
        char input[128];
        const auto count = std::fread(input, 1, sizeof(input), file);
        const bool failed = std::ferror(file) != 0;
        std::fclose(file);
        if (failed || count < 32 || count == sizeof(input)) return false;
        bool nonzero = false;
        for (unsigned i = 0; i < 32; ++i) {
            char c = input[i];
            if (c >= 'a' && c <= 'f') c -= 'a' - 'A';
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
            nonzero |= c != '0'; input[i] = c;
        }
        if (!nonzero) return false;
        for (auto i = std::size_t(32); i < count; ++i)
            if (input[i] != '\n' && input[i] != '\r' && input[i] != ' ' && input[i] != '\t') return false;
        Format128(input, output);
        return true;
    }
    // Representación determinista de los 128 bits, sin identificador constante de respaldo.
    static void Format128(const char (&input)[128], char (&output)[36]) {
        unsigned position = 0;
        for (unsigned i = 0; i < 32; ++i) {
            if (i && i % 8 == 0) output[position++] = '-';
            output[position++] = input[i];
        }
        output[position] = 0;
    }
#ifdef __APPLE__
    // macOS no tiene /etc/machine-id: la identidad de la máquina es el
    // IOPlatformUUID (estable por máquina), reducido a los mismos 128 bits.
    static bool ReadPlatformUUID(char (&output)[36]) {
        FILE* pipe = popen("ioreg -rd1 -c IOPlatformExpertDevice 2>/dev/null", "r");
        if (!pipe) return false;
        char buffer[4096];
        const auto count = std::fread(buffer, 1, sizeof(buffer) - 1, pipe);
        const int status = pclose(pipe);
        if (status != 0 || count == 0) return false;
        buffer[count] = 0;
        const char* key = std::strstr(buffer, "IOPlatformUUID");
        if (!key) return false;
        const char* eq = std::strchr(key, '=');
        const char* open = eq ? std::strchr(eq, '"') : nullptr;
        if (!open) return false;
        char input[128] = {};
        unsigned found = 0;
        bool nonzero = false;
        for (const char* p = open + 1; *p && *p != '"' && found < 128; ++p) {
            char c = *p;
            if (c == '-') continue;
            if (c >= 'a' && c <= 'f') c -= 'a' - 'A';
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
            nonzero |= c != '0';
            input[found++] = c;
        }
        if (found != 32 || !nonzero) return false;
        Format128(input, output);
        return true;
    }
#endif
public:
    explicit PosixIdentity(const char* first = "/etc/machine-id", const char* second = "/var/lib/dbus/machine-id")
        : primary(first), alternate(second) {}
    bool Read(char (&output)[36], const char*& error) override {
        output[0] = 0; error = nullptr;
#ifdef __APPLE__
        if (ReadPlatformUUID(output)) return true;
#endif
        if (ReadFile(primary, output) || ReadFile(alternate, output)) return true;
        error = "No hay machine-id válido en /etc/machine-id ni /var/lib/dbus/machine-id";
        return false;
    }
};
}
