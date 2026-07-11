// QuickFIX-cpp golden-authoring generator for o (RegistrationInstructions) —
// 069-v44-all-families T011/T012 [US2] (contracts/coverage-and-completeness.md
// C4, family class R-001 registration: NoRegistDtls nested group).
// OFFLINE TOOLING, not checked in to the main build (see gen/README.md).
//
// Required='Y' fields (dictionaries/FIX44.xml:1009-1021, FIX44::
// RegistrationInstructions ctor): RegistID(513), RegistTransType(514),
// RegistRefID(508). RgstDtlsGrp/NoRegistDtls(473) is required='N' but
// populated — 1 entry, RegistDtls(509) — per C4's table note.
#include <iostream>
#include <quickfix/fix44/RegistrationInstructions.h>

int main() {
    FIX44::RegistrationInstructions ri(
        FIX::RegistID("REGID1"),
        FIX::RegistTransType(FIX::RegistTransType_NEW),
        FIX::RegistRefID("REGREF1"));

    // NoRegistDtls (473, optional group — populated per C4's table note).
    {
        FIX44::RegistrationInstructions::NoRegistDtls dtl;
        dtl.set(FIX::RegistDtls("DETAILS1"));
        ri.addGroup(dtl);
    }

    ri.getHeader().setField(FIX::BeginString("FIX.4.4"));
    ri.getHeader().setField(FIX::SenderCompID("S"));
    ri.getHeader().setField(FIX::TargetCompID("T"));
    ri.getHeader().setField(FIX::MsgSeqNum(1));
    ri.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = ri.toString();
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
