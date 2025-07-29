#pragma once

#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/ASTTypeTraits.h"

namespace cpp2c
{
    class DeclStmtTypeLoc
    {
    private:
        inline void assertOneNonNull();

    public:
        const clang::Decl *D = nullptr;
        const clang::Stmt *ST = nullptr;
        const clang::TypeLoc *TL = nullptr;

        DeclStmtTypeLoc(const clang::Decl *D);
        DeclStmtTypeLoc(const clang::Stmt *ST);
        DeclStmtTypeLoc(const clang::TypeLoc *TL);

        void dump();

        clang::SourceRange getSourceRange();

        bool operator==(const DeclStmtTypeLoc &Other) const
        {
            return (D == Other.D && ST == Other.ST && TL == Other.TL);
        }

        clang::DynTypedNode getDynTypedNode() const
        {
            if (D)
                return clang::DynTypedNode::create(*D);
            else if (ST)
                return clang::DynTypedNode::create(*ST);
            else if (TL)
                return clang::DynTypedNode::create(*TL);
            else
                return clang::DynTypedNode();
        }
    };
} // namespace cpp2c
