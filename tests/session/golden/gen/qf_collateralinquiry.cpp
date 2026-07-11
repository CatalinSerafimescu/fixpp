// QuickFIX-cpp golden-authoring generator for BB (CollateralInquiry) —
// 069-v44-all-families T011/T012 [US2] (contracts/coverage-and-completeness.md
// C4, family class C-001 Collateral).
// OFFLINE TOOLING, not checked in to the main build (see gen/README.md).
//
// CollateralInquiry has NO required='Y' fields (dictionaries/FIX44.xml:
// 2231-2273 — every field/component is required='N'; confirmed against the
// QuickFIX header's default-constructible ctor, no required-args overload).
// Seed a sane illustrative set: Account(1), AccountType(581),
// CollInquiryID(909), NoCollInquiryQualifier(938, 1 entry).
#include <quickfix/fix44/CollateralInquiry.h>

#include <iostream>

int main() {
    FIX44::CollateralInquiry ci;
    ci.set(FIX::Account("ACCT1"));
    ci.set(FIX::AccountType(1));
    ci.set(FIX::CollInquiryID("COLLINQ1"));

    // NoCollInquiryQualifier (938, optional group).
    {
        FIX44::CollateralInquiry::NoCollInquiryQualifier qual;
        qual.set(FIX::CollInquiryQualifier(0));
        ci.addGroup(qual);
    }

    ci.getHeader().setField(FIX::BeginString("FIX.4.4"));
    ci.getHeader().setField(FIX::SenderCompID("S"));
    ci.getHeader().setField(FIX::TargetCompID("T"));
    ci.getHeader().setField(FIX::MsgSeqNum(1));
    ci.getHeader().setField(FIX::SendingTime(FIX::UtcTimeStamp(0, 0, 0, 1, 1, 2024)));

    std::string wire = ci.toString();
    for (char c : wire) std::cout << (c == '\x01' ? '|' : c);
    std::cout << "\n";
    return 0;
}
