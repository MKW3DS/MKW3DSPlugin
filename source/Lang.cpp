#include "Lang.hpp"
#include "CTRPluginFramework.hpp"
#include "3ds.h"
#include <algorithm>
#include "rt.h"

namespace CTRPluginFramework
{
    u32         Language::_currentLanguage = static_cast<u32>(LanguageId::English);
    LangMap     Language::_languages[12];
    LangMap&    Language::Japanese(_languages[0]);
    LangMap&    Language::English(_languages[1]);
    LangMap&    Language::French(_languages[2]);
    LangMap&    Language::German(_languages[3]);
    LangMap&    Language::Italian(_languages[4]);
    LangMap&    Language::Spanish(_languages[5]);
    LangMap&    Language::ChineseSimplified(_languages[6]);
    LangMap&    Language::Korean(_languages[7]);
    LangMap&    Language::Dutch(_languages[8]);
    LangMap&    Language::Portugese(_languages[9]);
    LangMap&    Language::Russian(_languages[10]);
    LangMap&    Language::ChineseTraditional(_languages[11]);
    u32*        Language::commonMsbtPtr = NULL;
    u32*        Language::menuMsbtPtr = NULL;

    Language::MsbtHandler* Language::commonMsbt = nullptr;
    Language::MsbtHandler* Language::menuMsbt = nullptr;

    static const char * const   g_filenames[] =
    {
        "Lang_JAP.txt",
        "Lang_ENG.txt",
        "Lang_FRA.txt",
        "Lang_DEU.txt",
        "Lang_ITA.txt",
        "Lang_SPA.txt",
        "Lang_CHN.txt",
        "Lang_KOR.txt",
        "Lang_DUT.txt",
        "Lang_POR.txt",
        "Lang_RUS.txt",
        "Lang_TWN.txt"
    };

    extern RT_HOOK getmsbtptrfromobjhook;

    void    Language::Initialize(void)
    {
        _currentLanguage = static_cast<u32>(System::GetSystemLanguage());

        // If a language file exists, load it
        if (File::Exists(std::string(LANGFILES) << g_filenames[_currentLanguage]))
        {
            _ImportLanguage();
            return;
        }
        // Else force our language to English
        _currentLanguage = 1;
        if (File::Exists(std::string(LANGFILES) << g_filenames[_currentLanguage]))
        {
            _ImportLanguage();
            return;
        } else {
            rtDisableHook(&getmsbtptrfromobjhook);
            return;
        }
    }

    const std::string&  Language::GetName(const std::string &key)
    {
        return (std::get<0>(_FindKey(key)));
    }

    const std::string&  Language::GetNote(const std::string &key)
    {
        return (std::get<1>(_FindKey(key)));
    }

    static std::tuple<std::string, std::string> empty = { "", "" };
    const Language::LangTuple& Language::_FindKey(const std::string &key)
    {
        LangMap     &currentMap = _languages[_currentLanguage];

        // Search for the requested string in the current map
        auto        str = currentMap.find(key);

        // If we found the requested string, returns it
        if (str != currentMap.end())
            return (str->second);

        // Since we couldn't find the requested string, returns an empty string
        return (empty);
    }

    static inline std::string &Ltrim(std::string &str)
    {
        auto it = std::find_if(str.begin(), str.end(), [](char ch) { return (!std::isspace(ch)); });
        str.erase(str.begin(), it);
        return (str);
    }

    static inline std::string &Rtrim(std::string &str)
    {
        auto it = std::find_if(str.rbegin(), str.rend(), [](char ch) { return (!std::isspace(ch)); });
        str.erase(it.base(), str.end());
        return (str);
    }

    static inline std::string &Trim(std::string &str)
    {
        return (Ltrim(Rtrim(str)));
    }

    bool isnumerical(const std::string &in, bool isHex) {
        for (const char c : in) {
            if (c < '0' || c > '9') {
                if (!isHex) return false;
                else if (!((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
            }
        }
        return true;
    }

    static u32 Str2U32(const std::string &str)
    {
        u32 val = 0;
        const u8 *hex = (const u8 *)str.c_str();

        if (str.size() < 4)
            return (val);

        while (*hex)
        {
            u8 byte = (u8)*hex++;

            if (byte >= '0' && byte <= '9') byte = byte - '0';
            else if (byte >= 'a' && byte <='f') byte = byte - 'a' + 10;
            else if (byte >= 'A' && byte <='F') byte = byte - 'A' + 10;
            else return (0); ///< Incorrect char

            val = (val << 4) | (byte & 0xF);
        }
        return (val);
    }

    std::string ConvertToUTF8(const std::string &str)
    {
        u32     code = 0;
        char    buffer[10] = {0};

        if (str[0] != '\\' && str[1] != 'u')
            return (buffer);

        code = Str2U32(str.substr(2, 4));
        encode_utf8((u8*)buffer, code);
        return (buffer);
    }

    static void     Process(std::string &str)
    {
        if (str.empty()) return;
        // Process our string
        for (u32 i = 0; i < str.size() - 1; i++)
        {

            const char c = str[i];

            // Check the symbol \n
            if (c == '\\' && str[i + 1] == 'n')
            {

                str[i] = '\n';

                str = str.erase(i + 1, 1);

                continue;
            }
            // Check the symbol \u
            if (c == '\\' && str[i + 1] == 'u')
            {
                std::string utf8 = ConvertToUTF8(str.substr(i));

                if (utf8.empty()) continue;

                str.erase(i, 6);

                str.insert(i, utf8);

                continue;
            }

            if (c != '\\' && str[i + 1] == '#') {
                str.erase(i + 1);
                continue;
            }
        }
    }

    void Language::_ImportLanguage(void)
    {

        std::string     line;
        File            file(std::string(LANGFILES) << g_filenames[_currentLanguage], File::READ);

        LineReader      reader(file);

        LangMap&        currentLanguage = _languages[_currentLanguage];

        std::string     key;
        std::string     name;
        std::string     note;

        // Read our file until the last line
        while (reader(line))
        {
            // If line is empty, skip it
            if (line.empty() || line[0] == '#') //Ignore lines with comments
                continue;

            key.clear();
            name.clear();
            note.clear();

            // Search our first delimiter
            auto namePos = line.find("::");

            // If we couldn't find the delimiter, skip the line
            if (namePos == std::string::npos)
                continue;

            // Else get our key
            key = line.substr(0, namePos);
            Trim(key);

            // Now let's increment our position to skip the ::
            namePos += 2;

            // Find the second delimiter
            auto notePos = line.find("::", namePos);

            // If we didn't find the delimiter, then there's no note
            if (notePos == std::string::npos)
            {
                name = line.substr(namePos);
                Trim(name);

            }
            // Get just the name, and then the note
            else
            {
                name = line.substr(namePos, notePos - namePos);
                Trim(name);

                // Skip the :: and retrieve our note
                note = line.substr(notePos + 2);
                Trim(note);
            }

            // Process our strings
            Process(name);
            Process(note);

            // Add them to our current map
            currentLanguage[key] = { name, note };
        }
    }

    void Language::applyMsbt(char id, Language::MsbtHandler& hnd) {

        LangMap&        currentLanguage = _languages[_currentLanguage];

        for (auto it = currentLanguage.cbegin(); it != currentLanguage.cend();)
        {
            if ((*it).first[0] == id)
            {
                std::string&& textid = (*it).first.substr(1);
                if (!isnumerical(textid, false)) continue;
                std::string name = std::get<0>((*it).second);
                hnd.applyStringText(std::stoi(textid), name);
                it = currentLanguage.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    Language::MsbtHandler::MsbtHandler(u32 *ptr) :
        msbtTablePtr(ptr), msbtCusPtr(*msbtTablePtr, 0), msbtOriPtr(*msbtTablePtr, 0), textEnabled(*msbtTablePtr, false)
    {
    }

    void ReplaceStringInPlace(std::string& subject, const std::string search, const std::string replace) {
        size_t pos = 0;

        while ((pos = subject.find(search, pos)) != std::string::npos) {
            subject.replace(pos, search.length(), replace);
            pos += replace.length();
        }
    }

    u32* Language::MsbtHandler::getTextPtrById(u32 id) {
        return msbtTablePtr + id + 1;
    }

    void Language::MsbtHandler::updatePtr(u32* newptr) {
        int offset = (int)msbtTablePtr - (int)newptr;
        msbtTablePtr = newptr;
        replaceAllIngameText(offset);
    }

    void Language::MsbtHandler::replaceAllIngameText(int offset) {
        if (!msbtTablePtr)
            return;
        if (offset) {
            for (int i = 0; i < *msbtTablePtr; i++) {
                if (msbtCusPtr[i]) {
                    msbtCusPtr[i] += offset;
                }
            }
        }
        for (int i = 0; i < *msbtTablePtr; i++) {
            if (textEnabled[i]) {
                replaceIngameText(i);
            }
        }
    }
    bool Language::MsbtHandler::replaceIngameText(u32 id, const u16* text) {
        if (!msbtTablePtr)
            return false;
        if (id - 1 > *msbtTablePtr)
            return false;

        u32* textptr = getTextPtrById(id);

        if (!text) {
            if (msbtCusPtr[id]) {
                if (!msbtOriPtr[id]) msbtOriPtr[id] = *textptr;
                textEnabled[id] = true;
                *textptr = msbtCusPtr[id];
                return true;
            }
            return false;
        }
        else {

            if (!msbtOriPtr[id]) msbtOriPtr[id] = *textptr;
            msbtCusPtr[id] = *textptr = (u32)text - (u32)msbtTablePtr;
            textEnabled[id] = true;
            return true;
        }
    }
    bool Language::MsbtHandler::restoreAllIngameText() {
        for (int i = 0; i < *msbtTablePtr; i++) {
            restoreIngameText(i);
        }
        return true;
    }
    bool Language::MsbtHandler::restoreIngameText(u32 id) {
        if (!msbtTablePtr)
            return false;
        if (id > *msbtTablePtr)
            return false;
        textEnabled[id] = false;
        if (!msbtOriPtr[id]) return true;
        *getTextPtrById(id) = msbtOriPtr[id];
        return true;
    }
    int utf8len(std::string &str)
    {
        u32 len = 0;
        u32 tmp = 0;
        u16 tmp2[2];
        u32 maxsize = str.size();
        const char* s = str.c_str();
        while (*s) {
            len++;
            int codelen = decode_utf8(&tmp, (u8*)s);
            if (codelen == -1) break;
            if (encode_utf16(tmp2, tmp) > 1) return -len;
            s += codelen;
        }
        return len;
    }
    bool Language::MsbtHandler::applyStringText(u32 id, std::string &str) {
        if (!msbtTablePtr)
            return false;

        if (id > *msbtTablePtr)
            return false;

        if (msbtCusPtr[id]) {
            restoreIngameText(id);
            delete[] (u16*)((u32)msbtTablePtr + msbtCusPtr[id]);
            msbtCusPtr[id] = 0;
            msbtOriPtr[id] = 0;
            textEnabled[id] = false;
        }

        int str_size = utf8len(str);
        if (str_size < 0) {
            OSD::Notify("Invalid character, char: " + std::to_string(-str_size) + ", id: $" + std::to_string(id));
            Sleep(Seconds(0.5));
            return false;
        }

        u16* textbuff = new u16[str_size + 1];

        if (!Process::WriteString((u32)textbuff, str, (str_size + 1) * 2, StringFormat::Utf16)) {
            if (textbuff) {
                delete[] textbuff;
            }
            return false;
        }
        return replaceIngameText(id, textbuff);
    }
}
