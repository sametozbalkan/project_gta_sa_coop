// Private build: policy test for the server's PED_SPAWN special character check without ENet (server/src/
// CSpecialActorPolicy.h, applied in server/src/PacketHandlers/peds.cpp): exact and case-insensitive matches, fields
// without a terminator, prefixes and longer names, every listed name, the log form, and the list against the names the
// scripts load (scm/main.txt and its includes). Build and run from the workspace root: see CLAUDE.md; the scripts are
// found next to this file, or pass the CoopAndreas folder as the first argument.
#include "../server/src/CSpecialActorPolicy.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

using namespace SpecialActorPolicy;

namespace
{
// specialModelName as Packets::Peds::PedSpawn carries it: 8 bytes, the name, then zeros (a name of 8 or more
// characters fills the field without a terminator)
struct SField
{
    char bytes[8];
};

SField Field(const char* name)
{
    SField field{};
    std::memcpy(field.bytes, name, std::min(std::strlen(name), sizeof(field.bytes)));
    return field;
}

bool Allowed(const char* name)
{
    const SField field = Field(name);
    return IsAllowed(field.bytes);
}

std::string Lower(std::string text)
{
    for (char& c : text)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

bool IsListed(const std::string& name)
{
    for (const char* listed : ALLOWED_NAMES)
    {
        if (name == listed)
        {
            return true;
        }
    }
    return false;
}

void ExactMatch()
{
    CHECK(Allowed("SWEET"));
    CHECK(Allowed("SMOKE"));
    CHECK(Allowed("SMOKEV"));
    CHECK(Allowed("CAT"));
    CHECK(Allowed("JANITOR"));  // 7 characters, the longest a field holds
    // upstream's check accepted all of these: strncasecmp's "differs" was taken for "matches"
    CHECK(!Allowed("XYZ"));
    CHECK(!Allowed("PLAYER"));
    CHECK(!Allowed("NULL"));
}

void CaseIgnored()
{
    CHECK(Allowed("sweet"));
    CHECK(Allowed("Sweet"));
    CHECK(Allowed("sWeEt"));
    CHECK(Allowed("smokev"));
    CHECK(Allowed("Ryder2"));
    // ASCII only: no other byte is folded onto a letter
    const SField latin1 = Field("SWE\xC9T");
    CHECK(!IsAllowed(latin1.bytes));
}

void Unterminated()
{
    // 8 bytes and no terminator: refused whatever they start with
    const char sweetAndMore[8] = {'S', 'W', 'E', 'E', 'T', 'S', 'W', 'E'};
    CHECK(!IsAllowed(sweetAndMore));
    const char janitorAndOne[8] = {'J', 'A', 'N', 'I', 'T', 'O', 'R', 'X'};
    CHECK(!IsAllowed(janitorAndOne));
    CHECK(!Allowed("SWEETSWEET"));
    // a shorter field: the terminator must lie inside it
    CHECK(!IsAllowed("SWEET", 5));
    CHECK(IsAllowed("SWEET", 6));
    CHECK(!IsAllowed("SWEET", 0));
    // the first terminator ends the name; bytes after it (left from an earlier, longer name) don't count
    const char catAfterSindaco[8] = {'C', 'A', 'T', '\0', 'A', 'C', 'O', '\0'};
    CHECK(IsAllowed(catAfterSindaco));
    const char splitSweet[8] = {'S', 'W', '\0', 'E', 'E', 'T', '\0', '\0'};
    CHECK(!IsAllowed(splitSweet));
}

void PrefixesAndLongerNames()
{
    CHECK(!Allowed(""));
    CHECK(!Allowed("S"));
    CHECK(!Allowed("SWEE"));
    CHECK(!Allowed("SMOK"));
    CHECK(!Allowed("RYDER"));
    CHECK(!Allowed("BB"));      // a prefix of BBTHIN; upstream's list had it, no script loads it
    CHECK(!Allowed("COPGRL"));
    CHECK(!Allowed("SWEETS"));
    CHECK(!Allowed("CATS"));
    CHECK(!Allowed("ZEROX"));
    CHECK(!Allowed("SMOKEVX"));
    CHECK(!Allowed("RYDER22"));
    CHECK(!Allowed(" SWEET"));
    CHECK(!Allowed("SWEET "));
    // upstream's list also had names the other games can't load as special characters: the girlfriends' IDE models
    // (peds.ide 190-195) and RYDER1, which gta3.img doesn't hold
    for (const char* name : {"COPGRL3", "CROGRL3", "GANGRL3", "GUNGRL3", "MECGRL3", "NURGRL3", "RYDER1"})
    {
        CHECK(!Allowed(name));
    }
}

void EveryListedName()
{
    std::set<std::string> seen;
    for (const char* name : ALLOWED_NAMES)
    {
        const std::string text = name;
        // the form the client sends: 1-7 characters, upper case
        CHECK(!text.empty() && text.size() <= MAX_NAME_LENGTH);
        for (const char c : text)
        {
            CHECK((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
        }
        CHECK(seen.insert(text).second);

        CHECK(Allowed(name));
        CHECK(Allowed(Lower(text).c_str()));
        // one character less passes only when that is a listed name too (SMOKEV -> SMOKE)
        const std::string shorter = text.substr(0, text.size() - 1);
        CHECK(Allowed(shorter.c_str()) == IsListed(shorter));
        CHECK(!Allowed((text + "X").c_str()));
    }
}

void LogForm()
{
    const SField sweet = Field("SWEET");
    CHECK(ForLog(sweet.bytes, sizeof(sweet.bytes)) == "SWEET");
    const SField empty = Field("");
    CHECK(ForLog(empty.bytes, sizeof(empty.bytes)).empty());
    const char control[8] = {'S', 'W', '\x1B', '[', '2', 'J', '\n', '\0'};
    CHECK(ForLog(control, sizeof(control)) == "SW?[2J?");
    const char unterminated[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
    CHECK(ForLog(unterminated, sizeof(unterminated)) == "ABCDEFGH...");
    const char high[8] = {'A', '\xFF', 'B', '\0'};
    CHECK(ForLog(high, sizeof(high)) == "A?B");
}

// --- the list against the scripts ----------------------------------------------------------------------------------

std::string Upper(std::string text)
{
    for (char& c : text)
    {
        c = ToUpperAscii(c);
    }
    return text;
}

// the line without its // comment (quotes respected)
std::string Code(const std::string& line)
{
    char quote = 0;
    for (size_t i = 0; i < line.size(); i++)
    {
        const char c = line[i];
        if (quote != 0)
        {
            if (c == quote)
            {
                quote = 0;
            }
        }
        else if (c == '\'' || c == '"')
        {
            quote = c;
        }
        else if (c == '/' && i + 1 < line.size() && line[i + 1] == '/')
        {
            return line.substr(0, i);
        }
    }
    return line;
}

std::string Directory(const std::string& path)
{
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

struct SScriptLoad
{
    std::string where;  // file:line
    std::string name;   // as the client sends it
};

// Every special character load in main.txt and the files it includes ({$INCLUDE ...}): Streaming.LoadSpecialCharacter
// and the raw 023C form. A load without a literal name can't be checked and fails the test.
void ReadScript(const std::string& path, const std::string& shownAs, std::set<std::string>& visited,
    std::vector<SScriptLoad>& loads, int& files)
{
    if (!visited.insert(Upper(path)).second)
    {
        return;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::fprintf(stderr, "can't read %s (run from the workspace root or pass the CoopAndreas folder)\n",
            path.c_str());
        std::exit(1);
    }
    files++;

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line))
    {
        lineNumber++;
        const std::string code = Code(line);
        const std::string upper = Upper(code);
        const std::string where = shownAs + ":" + std::to_string(lineNumber);

        const size_t include = upper.find("{$INCLUDE ");
        if (include != std::string::npos)
        {
            const size_t start = include + std::strlen("{$INCLUDE ");
            const size_t end = code.find('}', start);
            CHECK(end != std::string::npos);
            std::string target = code.substr(start, end - start);
            target.erase(target.find_last_not_of(" \t") + 1);
            ReadScript(Directory(path) + "/" + target, target, visited, loads, files);
            continue;
        }

        size_t command = upper.find("STREAMING.LOADSPECIALCHARACTER(");
        if (command == std::string::npos)
        {
            // the opcode form ("023C: load_special_actor 'SWEET' as 1") starts a statement
            const size_t first = upper.find_first_not_of(" \t");
            if (first != std::string::npos && upper.compare(first, 5, "023C:") == 0)
            {
                command = first;
            }
        }
        if (command == std::string::npos)
        {
            continue;
        }
        const size_t open = code.find_first_of("'\"", command);
        const size_t close = open == std::string::npos ? std::string::npos : code.find(code[open], open + 1);
        if (close == std::string::npos)
        {
            std::fprintf(stderr, "%s loads a special character without a literal name: %s\n", where.c_str(),
                line.c_str());
            std::exit(1);
        }
        // the client's form: upper case, at most 7 characters (PedHooks.cpp)
        loads.push_back({where, Upper(code.substr(open + 1, close - open - 1)).substr(0, MAX_NAME_LENGTH)});
    }
}

void ListMatchesScripts(const std::string& coopAndreasDir)
{
    std::set<std::string> visited;
    std::vector<SScriptLoad> loads;
    int files = 0;
    ReadScript(coopAndreasDir + "/scm/main.txt", "main.txt", visited, loads, files);
    CHECK(files > 1);
    CHECK(!loads.empty());

    std::map<std::string, std::string> loaded;  // name -> first file:line
    bool ok = true;
    for (const SScriptLoad& load : loads)
    {
        loaded.emplace(load.name, load.where);
        if (!IsListed(load.name))
        {
            std::fprintf(stderr, "%s loads special character \"%s\", which CSpecialActorPolicy.h doesn't list\n",
                load.where.c_str(), load.name.c_str());
            ok = false;
        }
    }
    for (const char* name : ALLOWED_NAMES)
    {
        if (loaded.find(name) == loaded.end())
        {
            std::fprintf(stderr, "CSpecialActorPolicy.h lists \"%s\", which no script loads\n", name);
            ok = false;
        }
    }
    CHECK(ok);
    std::printf("special_actor_test: %d script files, %zu special character loads, %zu names, all listed\n", files,
        loads.size(), loaded.size());
}
}  // namespace

int main(int argc, char** argv)
{
    ExactMatch();
    CaseIgnored();
    Unterminated();
    PrefixesAndLongerNames();
    EveryListedName();
    LogForm();
    ListMatchesScripts(argc > 1 ? std::string(argv[1]) : Directory(__FILE__) + "/..");
    std::printf("special_actor_test: all checks passed\n");
    return 0;
}
