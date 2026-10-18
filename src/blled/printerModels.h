#ifndef _BLLED_PRINTER_MODELS
#define _BLLED_PRINTER_MODELS

// Reading a Bambu Lab printer's discovery message (SSDP NOTIFY / M-SEARCH reply on UDP 2021):
// its serial (USN), model code (DevModel.bambu.com) and the name given to it in Bambu's app
// (DevName.bambu.com). No hardware dependencies, so it's tested on the host.

#include <string.h>
#include <ctype.h>

namespace bblssdp
{
    // Value of the header `name` ("Name: value\r\n", name case-insensitive), trimmed. False if absent.
    inline bool header(const char *msg, const char *name, char *out, size_t n)
    {
        out[0] = 0;
        const size_t nl = strlen(name);
        for (const char *line = msg; line && *line;)
        {
            if (strncasecmp(line, name, nl) == 0 && line[nl] == ':')
            {
                const char *v = line + nl + 1;
                while (*v == ' ' || *v == '\t') v++;
                size_t len = strcspn(v, "\r\n");
                while (len && (v[len - 1] == ' ' || v[len - 1] == '\t')) len--;
                if (len >= n) len = n - 1;
                while (len && ((unsigned char)v[len] & 0xC0) == 0x80) len--; // don't split a UTF-8 character
                memcpy(out, v, len);
                out[len] = 0;
                return true;
            }
            line = strchr(line, '\n');
            if (line) line++;
        }
        return false;
    }

    // DevModel codes mapped to the setup page's model keys. BL-P001 is confirmed on the author's X1
    // Carbon (2026-10-18); the others are from public integrations.
    // Other codes are passed through as they are, so the page shows them and keeps every option
    // (add a code here once it's confirmed on a real printer).
    struct ModelCode { const char *code, *key; };
    static const ModelCode MODEL_CODES[] = {
        {"BL-P001", "X1C"}, {"3DPrinter-X1-Carbon", "X1C"}, {"3DPrinter-X1", "X1"}, {"C13", "X1E"},
        {"C11", "P1P"}, {"C12", "P1S"}, {"N1", "A1M"}, {"N2S", "A1"}, {"O1D", "H2D"},
    };
    inline void modelKey(const char *code, char *out, size_t n)
    {
        for (const ModelCode &m : MODEL_CODES)
            if (strcmp(code, m.code) == 0) { strncpy(out, m.key, n - 1); out[n - 1] = 0; return; }
        size_t len = 0;   // unknown: pass through, printable ASCII only
        for (const char *p = code; *p && len < n - 1; p++)
            if (isalnum((unsigned char)*p) || *p == '-' || *p == '_') out[len++] = *p;
        out[len] = 0;
    }
}

#endif
