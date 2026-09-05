// Compile with the companion Firo source headers; see patches/README.md.
#include "script/script.h"

#include <algorithm>
#include <iostream>

int main()
{
    const auto check = [](CScript script, const std::string& message) {
        const CScript prefix = script;
        if (!message.empty())
            script << std::vector<unsigned char>(message.begin(), message.end());
        assert(script.size() <= 100);
        assert(std::equal(prefix.begin(), prefix.end(), script.begin()));
        if (!message.empty()) {
            CScript::const_iterator pc = script.begin() + prefix.size();
            opcodetype opcode;
            std::vector<unsigned char> payload;
            const bool parsed = script.GetOp(pc, opcode, payload);
            assert(parsed && pc == script.end());
            assert(std::string(payload.begin(), payload.end()) == message);
        }
        return script.size();
    };

    for (int length : {0, 1, 75, 76, 80}) {
        const std::string message(length, 'x');
        const auto size = check(CScript() << OP_RETURN, message);
        assert(size == 1 + length + (length == 0 ? 0 : length < 76 ? 1 : 2));
        check(CScript() << 600000 << OP_0, message);
    }
    check(CScript() << OP_RETURN, "Mined by caf\xc3\xa9");
    check(CScript() << OP_RETURN, std::string("nul\0byte", 8));
    CScript full;
    full.resize(18);
    const auto fullSize = check(full, std::string(80, 'x'));
    assert(fullSize == 100);
    std::cout << "Coinbase script checks passed.\n";
}
