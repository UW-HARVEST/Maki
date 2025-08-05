#include "AlignmentMatchers.hh"
#include "ExpansionMatchHandler.hh"
#include <stack>

namespace cpp2c
{

    void storeChildren(cpp2c::DeclStmtTypeLoc DSTL,
                       std::set<const clang::Stmt *> &MatchedStmts,
                       std::set<const clang::Decl *> &MatchedDecls,
                       std::set<const clang::TypeLoc *> &MatchedTypeLocs)
    {
        if (DSTL.ST)
        {
            std::stack<const clang::Stmt *> Descendants;
            Descendants.push(DSTL.ST);
            while (!Descendants.empty())
            {
                auto Cur = Descendants.top();
                Descendants.pop();
                if (!Cur)
                    continue;

                // llvm::errs() << "Inserting:\n";
                // Cur->dumpColor();
                MatchedStmts.insert(Cur);
                for (auto &&child : Cur->children())
                    if (child)
                        Descendants.push(child);
            }
        }
        else if (DSTL.D)
        {
            // llvm::errs() << "Inserting:\n";
            // DSTL.D->dump();
            MatchedDecls.insert(DSTL.D);
        }
        // else if (DSTL.TL)
        // {
        //     // TODO: Determine why this must be commented out to be able to
        //     //       correctly match TypeLocs
        //     // llvm::errs() << "Inserting:\n";
        // {
        //     auto QT = DSTL.TL->getType();
        //     if (!QT.isNull())
        //         QT.dump();
        //     else
        //         llvm::errs() << "<Null type>\n";
        // }
        //     MatchedTypeLocs.insert(DSTL.TL);
        // }
    }

    template <typename NodeT>
    static std::vector<clang::DynTypedNode>
    collectAncestors(const NodeT &Start, clang::ASTContext &Ctx)
    {
        std::vector<clang::DynTypedNode> Chain;
        clang::DynTypedNodeList Parents = Ctx.getParents(Start);
        while (!Parents.empty()) {
            clang::DynTypedNode Parent = Parents[0];
            Chain.push_back(Parent);
            Parents = Ctx.getParents(Parent);
        }
        return Chain;
    }

    void findAlignedASTNodesForExpansion(
        cpp2c::MacroExpansionNode *Exp,
        clang::ASTContext &Ctx)
    {
        const static bool debug = false;

        using namespace clang::ast_matchers;
        // Find AST nodes aligned with the entire invocation

        // Match stmts (including exprs)
        {
            MatchFinder Finder;
            ExpansionMatchHandler Handler;
            auto Matcher = stmt(unless(anyOf(implicitCastExpr(),
                                             implicitValueInitExpr())),
                                alignsWithExpansion(&Ctx, Exp))
                               .bind("root");
            Finder.addMatcher(Matcher, &Handler);
            Finder.matchAST(Ctx);
            for (auto &&M : Handler.Matches)
                Exp->ASTRoots.push_back(M);
        }

        // Match decls
        {
            MatchFinder Finder;
            ExpansionMatchHandler Handler;
            auto Matcher = decl(alignsWithExpansion(&Ctx, Exp))
                               .bind("root");
            Finder.addMatcher(Matcher, &Handler);
            Finder.matchAST(Ctx);
            for (auto &&M : Handler.Matches)
                Exp->ASTRoots.push_back(M);
        }

        // Match type locs
        {
            MatchFinder Finder;
            ExpansionMatchHandler Handler;
            auto Matcher = typeLoc(alignsWithExpansion(&Ctx, (Exp)))
                               .bind("root");
            Finder.addMatcher(Matcher, &Handler);
            Finder.matchAST(Ctx);
            for (auto &&M : Handler.Matches)
                Exp->ASTRoots.push_back(M);
        }

        // // Match all categories of AST nodes (Not working)
        // {
        //     using clang::ast_matchers::internal::DynTypedMatcher;
        //     MatchFinder Finder;
        //     ExpansionMatchHandler Handler;
        //     Finder.addMatcher
        //     (
        //         DynTypedMatcher(
        //             alignsWithExpansionAllCats(&Ctx, Exp)
        //         ),
        //         &Handler
        //     );
        //     Finder.matchAST(Ctx);
        //     for (auto &&M : Handler.Matches)
        //     {
        //         Exp->ASTRoots.push_back(M);
        //     }
        // }

        // Remove any ASTRoots that are descendants of other ASTRoots
        // We want to make sure there are only top-level nodes
        std::vector<cpp2c::DeclStmtTypeLoc> TopLevelRoots;
        for (auto && Child : Exp->ASTRoots)
        {
            bool IsDescendant = false;
            for (auto && PossibleAncestor : Exp->ASTRoots)
            {
                for (auto Ancestor : collectAncestors(Child.getDynTypedNode(), Ctx))
                {
                    if (Ancestor == PossibleAncestor.getDynTypedNode())
                    {
                        IsDescendant = true;
                        if (debug)
                        {
                            llvm::errs() << "Descendant removed:\n";
                            // Category
                            if (Child.ST)
                                llvm::errs() << "  Stmt: ";
                            else if (Child.D)
                                llvm::errs() << "  Decl: ";
                            else if (Child.TL)
                                llvm::errs() << "  TypeLoc: ";
                            else
                                llvm::errs() << "  Unknown: ";
                            llvm::errs() << "  ";
                            clang::PrintingPolicy Policy(Ctx.getLangOpts());
                            Child.getDynTypedNode().print(llvm::errs(), Policy);
                            llvm::errs() << "  is a descendant of:\n";
                            // Category
                            if (PossibleAncestor.ST)
                                llvm::errs() << "  Stmt: ";
                            else if (PossibleAncestor.D)
                                llvm::errs() << "  Decl: ";
                            else if (PossibleAncestor.TL)
                                llvm::errs() << "  TypeLoc: ";
                            else
                                llvm::errs() << "  Unknown: ";
                            llvm::errs() << "  ";
                            PossibleAncestor.getDynTypedNode().print(llvm::errs(), Policy);
                            llvm::errs() << "\n";
                        }
                        break;
                    }
                }
            }
            if (!IsDescendant)
            {
                TopLevelRoots.push_back(Child);
            }
        }
        Exp->ASTRoots = std::move(TopLevelRoots);

        if (debug)
        {
            if (Exp->ASTRoots.size() > 1)
            {
                llvm::errs() << "Multiple AST nodes aligned with "
                             << Exp->Name << ":\n";
            }

            llvm::errs() << "Matched " << Exp->ASTRoots.size()
                         << " top-level AST nodes for "
                         << Exp->Name << " at " << Exp->SpellingRange.getBegin().printToString(Ctx.getSourceManager())
                         << ":\n";
            for (auto &&ASTRoot : Exp->ASTRoots)
            {
                // Print source range lin:col
                auto SR = ASTRoot.getSourceRange();
                auto BeginLoc = SR.getBegin();
                auto EndLoc = SR.getEnd();
                auto BeginLine = Ctx.getSourceManager().getSpellingLineNumber(BeginLoc);
                auto BeginCol = Ctx.getSourceManager().getSpellingColumnNumber(BeginLoc);
                auto EndLine = Ctx.getSourceManager().getSpellingLineNumber(EndLoc);
                auto EndCol = Ctx.getSourceManager().getSpellingColumnNumber(EndLoc);
                llvm::errs() << BeginLine << ":" << BeginCol << "-"
                             << EndLine << ":" << EndCol;

                // Category
                if (ASTRoot.ST)
                    llvm::errs() << "  Stmt: ";
                else if (ASTRoot.D)
                    llvm::errs() << "  Decl: ";
                else if (ASTRoot.TL)
                    llvm::errs() << "  TypeLoc: ";
                else
                    llvm::errs() << "  Unknown: ";
                llvm::errs() << "  ";
                clang::PrintingPolicy Policy(Ctx.getLangOpts());
                ASTRoot.getDynTypedNode().print(llvm::errs(), Policy);
                llvm::errs() << "\n";
            }
        }

        // If the expansion only aligns with one node, then set this
        // as its aligned root
        Exp->AlignedRoot = (Exp->ASTRoots.size() == 1)
                               ? (&(Exp->ASTRoots.front()))
                               : nullptr;

        //// Find AST nodes aligned with each of the expansion's arguments

        for (auto &&Arg : Exp->Arguments)
        {
            // Match stmts
            {
                MatchFinder Finder;
                ExpansionMatchHandler Handler;
                auto Matcher = stmt(unless(anyOf(implicitCastExpr(),
                                                 implicitValueInitExpr())),
                                    isSpelledFromTokens(&Ctx, Arg.Tokens))
                                   .bind("root");
                Finder.addMatcher(Matcher, &Handler);
                Finder.matchAST(Ctx);
                for (auto &&M : Handler.Matches)
                    Arg.AlignedRoots.push_back(M);
            }

            // Match decls
            {
                MatchFinder Finder;
                ExpansionMatchHandler Handler;
                auto Matcher = decl(isSpelledFromTokens(&Ctx, Arg.Tokens))
                                   .bind("root");
                Finder.addMatcher(Matcher, &Handler);
                Finder.matchAST(Ctx);
                for (auto &&M : Handler.Matches)
                    Arg.AlignedRoots.push_back(M);
            }

            // Match type locs
            {
                MatchFinder Finder;
                ExpansionMatchHandler Handler;
                auto Matcher =
                    typeLoc(isSpelledFromTokens(&Ctx, Arg.Tokens))
                        .bind("root");
                Finder.addMatcher(Matcher, &Handler);
                Finder.matchAST(Ctx);
                for (auto &&M : Handler.Matches)
                    Arg.AlignedRoots.push_back(M);
            }
        }
    }
} // namespace cpp2c
