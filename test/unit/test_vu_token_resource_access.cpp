#include "test_harness.h"

#include "../../src/Error.h"
#include "../../src/File.h"
#include "../../src/Line.h"
#include "../../src/Operand.h"
#include "../../src/Token.h"
#include "../../src/Tokenizer.h"
#include "../../src/VuInstructionInfo.h"
#include "../../src/VuTokenResourceAccess.h"

#include <list>
#include <string>

namespace
{
    std::list<vcl::Operand> makeVuOperands()
    {
        std::list<vcl::Operand> ops;
        for (const vcl::VuInstructionInfo* info = vcl::allVuInstructionInfos(); info->mnemonic; ++info)
        {
            ops.push_back(vcl::Operand(info->operandName,
                                       info->arguments,
                                       info->operandFlags,
                                       info->operandPattern,
                                       info->operandUnit,
                                       info->throughput,
                                       info->latency));
        }
        return ops;
    }

    bool accessFor(const std::string& text, vcl::VuTokenResourceAccess& access)
    {
        static vcl::File f("<resource-test>");
        vcl::Tokenizer tokenizer;
        std::list<vcl::Operand> ops = makeVuOperands();
        tokenizer.setOperands(ops);
        tokenizer.setNewSyntax(true);
        vcl::Line line(f, 1, 1, text);
        vcl::Error::ResetErrorCount();
        if (!tokenizer.parse(line))
            return false;
        if (tokenizer.tokens().size() != 1u)
            return false;
        return vcl::buildVuTokenResourceAccess(tokenizer.tokens().front(), access);
    }

    bool hasKey(const std::list<std::string>& keys, const std::string& key)
    {
        for (std::list<std::string>::const_iterator i = keys.begin(); i != keys.end(); ++i)
        {
            if (*i == key)
                return true;
        }
        return false;
    }
}

TEST_CASE("VuTokenResourceAccess: broadcast VF reads use the broadcast component")
{
    vcl::VuTokenResourceAccess access;
    REQUIRE(accessFor("mulw.xyz vf01, vf02, vf03", access));
    CHECK(hasKey(access.registerWrites, "VF01.x"));
    CHECK(hasKey(access.registerWrites, "VF01.y"));
    CHECK(hasKey(access.registerWrites, "VF01.z"));
    CHECK(!hasKey(access.registerWrites, "VF01.w"));
    CHECK(hasKey(access.registerReads, "VF03.w"));
    CHECK(!hasKey(access.registerReads, "VF03.x"));
}

TEST_CASE("VuTokenResourceAccess: special registers and waits come from metadata and operands")
{
    vcl::VuTokenResourceAccess loiAccess;
    REQUIRE(accessFor("loi 1.0", loiAccess));
    CHECK((loiAccess.implicitWrites & vcl::VU_RESOURCE_I) != 0);

    vcl::VuTokenResourceAccess addiAccess;
    REQUIRE(accessFor("addi.xy vf01, vf02, i", addiAccess));
    CHECK((addiAccess.implicitReads & vcl::VU_RESOURCE_I) != 0);

    vcl::VuTokenResourceAccess divAccess;
    REQUIRE(accessFor("div q, vf01[x], vf02[x]", divAccess));
    CHECK((divAccess.implicitWrites & vcl::VU_RESOURCE_Q) != 0);

    vcl::VuTokenResourceAccess addqAccess;
    REQUIRE(accessFor("addq vf01, vf02, q", addqAccess));
    CHECK((addqAccess.implicitReads & vcl::VU_RESOURCE_Q) != 0);
}

TEST_CASE("VuTokenResourceAccess: MAC/CLIP, memory, branch, and bypass metadata are visible")
{
    vcl::VuTokenResourceAccess clipAccess;
    REQUIRE(accessFor("clipw.xyz vf01, vf02[w]", clipAccess));
    CHECK((clipAccess.implicitWrites & vcl::VU_RESOURCE_MAC) != 0);
    CHECK((clipAccess.implicitWrites & vcl::VU_RESOURCE_CLIP) != 0);

    vcl::VuTokenResourceAccess fcandAccess;
    REQUIRE(accessFor("fcand vi01, 0x3ffff", fcandAccess));
    CHECK((fcandAccess.implicitReads & vcl::VU_RESOURCE_CLIP) != 0);

    vcl::VuTokenResourceAccess lqiAccess;
    REQUIRE(accessFor("lqi.xyzw vf04, (vi01++)", lqiAccess));
    CHECK(lqiAccess.memoryKind == vcl::VU_MEMORY_LOAD);
    CHECK((lqiAccess.memoryFlags & vcl::VU_MEMORY_FLAG_POSTINC) != 0);

    vcl::VuTokenResourceAccess branchAccess;
    REQUIRE(accessFor("ibne vi01, vi02, target", branchAccess));
    CHECK(branchAccess.branchDelaySlots == 1);

    vcl::VuTokenResourceAccess ftoiAccess;
    REQUIRE(accessFor("ftoi4 vf01, vf02", ftoiAccess));
    CHECK((ftoiAccess.bypassFlags & vcl::VU_BYPASS_FTOI_TO_MTIR) != 0);
}
