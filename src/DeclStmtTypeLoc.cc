#include "DeclStmtTypeLoc.hh"

#include "assert.h"

#include <utility>

namespace cpp2c
{
    DeclStmtTypeLoc::DeclStmtTypeLoc(const clang::Decl *D) : D(D)
    {
        syncTypeLocPointer();
        assertOneNonNull();
    }

    DeclStmtTypeLoc::DeclStmtTypeLoc(const clang::Stmt *ST) : ST(ST)
    {
        syncTypeLocPointer();
        assertOneNonNull();
    }

    DeclStmtTypeLoc::DeclStmtTypeLoc(const clang::TypeLoc *TL)
    {
        if (TL)
        {
            StoredTypeLoc = *TL;
        }
        syncTypeLocPointer();
        assertOneNonNull();
    }

    DeclStmtTypeLoc::DeclStmtTypeLoc(const DeclStmtTypeLoc &Other)
        : D(Other.D),
          ST(Other.ST),
          StoredTypeLoc(Other.StoredTypeLoc)
    {
        syncTypeLocPointer();
        assertOneNonNull();
    }

    DeclStmtTypeLoc::DeclStmtTypeLoc(DeclStmtTypeLoc &&Other) noexcept
        : D(Other.D),
          ST(Other.ST),
          StoredTypeLoc(std::move(Other.StoredTypeLoc))
    {
        syncTypeLocPointer();
        Other.D = nullptr;
        Other.ST = nullptr;
        Other.TL = nullptr;
        Other.StoredTypeLoc.reset();
    }

    DeclStmtTypeLoc &DeclStmtTypeLoc::operator=(const DeclStmtTypeLoc &Other)
    {
        if (this == &Other)
            return *this;

        D = Other.D;
        ST = Other.ST;
        StoredTypeLoc = Other.StoredTypeLoc;
        syncTypeLocPointer();
        assertOneNonNull();
        return *this;
    }

    DeclStmtTypeLoc &DeclStmtTypeLoc::operator=(DeclStmtTypeLoc &&Other) noexcept
    {
        if (this == &Other)
            return *this;

        D = Other.D;
        ST = Other.ST;
        StoredTypeLoc = std::move(Other.StoredTypeLoc);
        syncTypeLocPointer();

        Other.D = nullptr;
        Other.ST = nullptr;
        Other.TL = nullptr;
        Other.StoredTypeLoc.reset();
        assertOneNonNull();
        return *this;
    }

    inline void DeclStmtTypeLoc::assertOneNonNull()
    {
        const int Count = (D ? 1 : 0) + (ST ? 1 : 0) + (TL ? 1 : 0);
        assert(Count <= 1 && "More than one type");
    }

    inline void DeclStmtTypeLoc::syncTypeLocPointer()
    {
        TL = StoredTypeLoc ? &*StoredTypeLoc : nullptr;
    }

    void DeclStmtTypeLoc::dump()
    {
        assertOneNonNull();
        if (D)
            D->dump();
        else if (ST)
            ST->dump();
        else if (TL)
        {
            auto QT = TL->getType();
            if (!QT.isNull())
                QT.dump();
            else
                llvm::errs() << "<Null type>\n";
        }
        else
            assert(!"No node to dump");
    }

    clang::SourceRange DeclStmtTypeLoc::getSourceRange()
    {
        assertOneNonNull();
        if (D)
            return D->getSourceRange();
        else if (ST)
            return ST->getSourceRange();
        else if (TL)
            return TL->getSourceRange();
        else
            assert(!"No node to dump");
    }
} // namespace cpp2c
