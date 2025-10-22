#include "AlignmentMatchers.hh"
#include "ExpansionMatchHandler.hh"
#include "Cpp2CASTConsumer.hh"
#include <stack>

namespace cpp2c
{
    // Returns true if there exists any non-comment token in [A, B), using spelling locations.
    // If locations are invalid, equal, different files, or ordered incorrectly, returns false.
    static bool hasNonCommentTokensBetween(
        clang::SourceLocation A,
        clang::SourceLocation B,
        clang::ASTContext &Ctx,
        bool useFileLoc = false)
    {
        const static bool debug = false;

        auto &SM = Ctx.getSourceManager();
        auto &LO = Ctx.getLangOpts();

        if (A.isInvalid() || B.isInvalid())
            return false;

        // Normalize to desired location space
        if (useFileLoc)
        {
            A = SM.getFileLoc(A);
            B = SM.getFileLoc(B);
        }
        else
        {
            A = SM.getSpellingLoc(A);
            B = SM.getSpellingLoc(B);
        }

        auto [FileIDA, OffsetA] = SM.getDecomposedLoc(A);
        auto [FileIDB, OffsetB] = SM.getDecomposedLoc(B);

        // Must be in the same file buffer
        if (FileIDA != FileIDB)
            return false; // Be permissive across files

        // Half-open [A, B)
        if (OffsetA >= OffsetB)
            return false;

        bool Invalid = false;
        llvm::StringRef Buffer = SM.getBufferData(FileIDA, &Invalid);
        if (Invalid)
            return false;

        const char *BufBegin = Buffer.begin();
        const char *TokStart = BufBegin + OffsetA;
        const char *TokEnd = BufBegin + OffsetB;

        clang::Lexer Lex(SM.getLocForStartOfFile(FileIDA), LO, BufBegin, TokStart, TokEnd);
        // Ensure raw lexing (no macro expansion). LexFromRawLexer already does raw.
        clang::Token Tok;
        while (!Lex.LexFromRawLexer(Tok))
        {
            if (Tok.is(clang::tok::eof))
                break;
            // Enforce half-open [A, B): if current token starts at or beyond B, stop.
            clang::SourceLocation TokLoc = Tok.getLocation();
            auto [TokFileID, TokOff] = SM.getDecomposedLoc(useFileLoc ? SM.getFileLoc(TokLoc)
                                                                      : SM.getSpellingLoc(TokLoc));
            if (TokFileID != FileIDA || TokOff >= OffsetB)
                break;
            if (Tok.is(clang::tok::comment))
            {
                if (debug)
                {
                    llvm::errs() << "Ignoring comment token between locations:\n";
                    A.dump(SM);
                    B.dump(SM);
                    llvm::errs() << "Token: " << Tok.getName() << "\n";
                }
                continue;
            }
            if (true)
            {
                if (debug)
                {
                    llvm::errs() << "Found non-comment token between locations:\n";
                    A.dump(SM);
                    B.dump(SM);
                    llvm::errs() << "Token: " << Tok.getName() << "\n";
                    TokLoc.dump(SM);
                }
                return true;
            }
        }
        return false;
    }

    // If the next token at Pos is a semicolon and within Limit, advance Pos
    // to after that semicolon. Returns true if swallowed.
    static bool swallowSingleTrailingSemicolon(
        clang::SourceLocation &Pos,
        clang::SourceLocation Limit,
        clang::ASTContext &Ctx)
    {
        auto &SM = Ctx.getSourceManager();
        auto &LO = Ctx.getLangOpts();

        if (Pos.isInvalid() || Limit.isInvalid())
            return false;

        // Normalize to FILE locations for range checking
        auto A = SM.getFileLoc(Pos);
        auto B = SM.getFileLoc(Limit);

        auto [FileIDA, OffsetA] = SM.getDecomposedLoc(A);
        auto [FileIDB, OffsetB] = SM.getDecomposedLoc(B);
        if (FileIDA != FileIDB || OffsetA >= OffsetB)
            return false;

        bool Invalid = false;
        llvm::StringRef Buffer = SM.getBufferData(FileIDA, &Invalid);
        if (Invalid)
            return false;

        const char *BufBegin = Buffer.begin();
        const char *Cur = BufBegin + OffsetA;
        const char *End = BufBegin + OffsetB;

        clang::Lexer Lex(SM.getLocForStartOfFile(FileIDA), LO, BufBegin, Cur, End);
        clang::Token Tok;
        if (!Lex.LexFromRawLexer(Tok))
        {
            if (Tok.is(clang::tok::semi))
            {
                // Compute end offset using token length
                auto TokLoc = Tok.getLocation();
                auto [FileIDT, OffsetT] = SM.getDecomposedLoc(SM.getFileLoc(TokLoc));
                if (FileIDT == FileIDA)
                {
                    unsigned SemiEndOff = OffsetT + Tok.getLength();
                    if (SemiEndOff <= (unsigned)OffsetB)
                    {
                        Pos = SM.getComposedLoc(FileIDA, SemiEndOff);
                        return true;
                    }
                }
            }
        }
        return false;
    }

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
                                             implicitValueInitExpr(),
                                             designatedInitExpr())),
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

        // Before committing, require that there are no extra non-comment tokens
        // at the leading/trailing edges outside the top-level roots within the
        // macro definition token range (not the call-site range!).
        if (!TopLevelRoots.empty())
        {
            auto &SM = Ctx.getSourceManager();
            auto &LO = Ctx.getLangOpts();

            // Prefer macro definition token boundaries to avoid capturing
            // trailing tokens from the call site (e.g., an extra ';').
            clang::SourceLocation MacroDefB;
            clang::SourceLocation MacroDefEEndTok;
            if (!Exp->DefinitionTokens.empty())
            {
                MacroDefB = SM.getSpellingLoc(Exp->DefinitionTokens.front().getLocation());
                auto LastTokSp = SM.getSpellingLoc(Exp->DefinitionTokens.back().getLocation());
                MacroDefEEndTok = clang::Lexer::getLocForEndOfToken(LastTokSp, 0, SM, LO);
            }
            else
            {
                // Fallback to the expansion spelling range if definition tokens are unavailable
                MacroDefB = SM.getSpellingLoc(Exp->SpellingRange.getBegin());
                auto ExpSpE = SM.getSpellingLoc(Exp->SpellingRange.getEnd());
                MacroDefEEndTok = clang::Lexer::getLocForEndOfToken(ExpSpE, 0, SM, LO);
            }

            // Compute min begin and max end among top-level roots (in spelling space)
            clang::SourceLocation MinBegin;
            clang::SourceLocation MaxEnd;
            bool first = true;
            for (auto &&Root : TopLevelRoots)
            {
                auto SR = Root.getSourceRange();
                clang::SourceLocation B = SM.getSpellingLoc(SR.getBegin());
                clang::SourceLocation E = SM.getSpellingLoc(SR.getEnd());
                if (first)
                {
                    MinBegin = B;
                    MaxEnd = E;
                    first = false;
                }
                else
                {
                    if (SM.isBeforeInTranslationUnit(B, MinBegin))
                        MinBegin = B;
                    if (SM.isBeforeInTranslationUnit(MaxEnd, E))
                        MaxEnd = E;
                }
            }

            // Convert MaxEnd to end-of-token
            clang::SourceLocation MaxEndEndTok = clang::Lexer::getLocForEndOfToken(MaxEnd, 0, SM, LO);

            bool leadingExtra = hasNonCommentTokensBetween(MacroDefB, MinBegin, Ctx);
            bool trailingExtra = hasNonCommentTokensBetween(MaxEndEndTok, MacroDefEEndTok, Ctx);

            if (leadingExtra || trailingExtra)
            {
                if (debug)
                {
                    llvm::errs() << "Filtered ASTRoots due to extra tokens at edges for expansion "
                                 << Exp->Name << " at "
                                 << Exp->SpellingRange.getBegin().printToString(SM) << "\n";
                }
                TopLevelRoots.clear();
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

    std::vector<DeclStmtTypeLoc> findAlignedASTNodesForCodeRange
    (
        const CodeRangeAnalysisTask & Task,
        clang::ASTContext & Ctx
    )
    {
        const static bool debug = false;

        clang::SourceManager & SM = Ctx.getSourceManager();

        clang::SourceRange Range = Task.getSourceRange(SM);
        std::vector<DeclStmtTypeLoc> AlignedASTNodes;

        using namespace clang::ast_matchers;
        // Find AST nodes aligned with the entire invocation

        // Match stmts (including exprs)
        {
            MatchFinder Finder;
            ExpansionMatchHandler Handler;
            auto Matcher = stmt(unless(anyOf(implicitCastExpr(),
                                             implicitValueInitExpr(),
                                             designatedInitExpr())),
                                alignsWithRange(&Ctx, Range))
                               .bind("root");
            Finder.addMatcher(Matcher, &Handler);
            Finder.matchAST(Ctx);
            for (auto &&M : Handler.Matches)
                AlignedASTNodes.push_back(M);
        }

        // Match decls
        {
            MatchFinder Finder;
            ExpansionMatchHandler Handler;
            auto Matcher = decl(alignsWithRange(&Ctx, Range))
                               .bind("root");
            Finder.addMatcher(Matcher, &Handler);
            Finder.matchAST(Ctx);
            for (auto &&M : Handler.Matches)
                AlignedASTNodes.push_back(M);
        }

        // Match type locs
        {
            MatchFinder Finder;
            ExpansionMatchHandler Handler;
            auto Matcher = typeLoc(alignsWithRange(&Ctx, Range))
                               .bind("root");
            Finder.addMatcher(Matcher, &Handler);
            Finder.matchAST(Ctx);
            for (auto &&M : Handler.Matches)
                AlignedASTNodes.push_back(M);
        }

        // Remove any ASTRoots that are descendants of other ASTRoots
        // We want to make sure there are only top-level nodes
        std::vector<cpp2c::DeclStmtTypeLoc> TopLevelRoots;
        for (auto && Child : AlignedASTNodes)
        {
            bool IsDescendant = false;
            for (auto && PossibleAncestor : AlignedASTNodes)
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
        // Before committing, require that there are no extra non-comment tokens
        // at the leading/trailing edges outside the top-level roots within the
        // provided code range.
        if (!TopLevelRoots.empty())
        {
            auto &LO = Ctx.getLangOpts();
            // Use the original range boundaries strictly (do not remap to definition)
            clang::SourceLocation RangeB = Range.getBegin();
            clang::SourceLocation RangeE = Range.getEnd();
            clang::SourceLocation RangeEEndTok = clang::Lexer::getLocForEndOfToken(RangeE, 0, SM, LO);

            clang::SourceLocation MinBegin;
            clang::SourceLocation MaxEnd;
            bool first = true;
            for (auto &&Root : TopLevelRoots)
            {
                auto SR = Root.getSourceRange();
                // // Use expansion locations so tokens expanded from macros are
                // // measured in the range's file space
                clang::SourceLocation B = SM.getExpansionLoc(SR.getBegin());
                clang::SourceLocation E = SM.getExpansionLoc(SR.getEnd());                
                // Use getExpansionRange in case the expansion has multiple tokens (e.g., macro with parameters)
                clang::CharSourceRange ER = SM.getExpansionRange(SR);
                E = ER.getEnd();

                if (first)
                {
                    MinBegin = B;
                    MaxEnd = E;
                    first = false;
                }
                else
                {
                    if (SM.isBeforeInTranslationUnit(B, MinBegin))
                        MinBegin = B;
                    if (SM.isBeforeInTranslationUnit(MaxEnd, E))
                        MaxEnd = E;
                }
            }

            clang::SourceLocation MaxEndEndTok = clang::Lexer::getLocForEndOfToken(MaxEnd, 0, SM, LO);
            // Swallow a single trailing semicolon immediately following the
            // top-level node(s), if it exists within the range's end.
            swallowSingleTrailingSemicolon(MaxEndEndTok, RangeEEndTok, Ctx);

            bool leadingExtra = hasNonCommentTokensBetween(RangeB, MinBegin, Ctx, /*useFileLoc=*/true);
            bool trailingExtra = hasNonCommentTokensBetween(MaxEndEndTok, RangeEEndTok, Ctx, /*useFileLoc=*/true);

            if (leadingExtra || trailingExtra)
            {
                if (debug)
                {
                    llvm::errs() << "Filtered aligned nodes due to extra tokens at edges for range "
                                 << Task.toString();
                }
                TopLevelRoots.clear();
            }
        }

        AlignedASTNodes = std::move(TopLevelRoots);

        if (debug)
        {
            if (AlignedASTNodes.size() > 1)
            {
                llvm::errs() << "Multiple AST nodes aligned with "
                             << Task.toString();
            }

            llvm::errs() << "Matched " << AlignedASTNodes.size()
                         << " top-level AST nodes for "
                         << Task.toString() << " at "
                         << Task.getSourceRange(SM).getBegin().printToString(SM)
                         << ":\n";
            for (auto &&ASTRoot : AlignedASTNodes)
            {
                // Print source range lin:col
                auto SR = ASTRoot.getSourceRange();
                auto BeginLoc = SR.getBegin();
                auto EndLoc = SR.getEnd();
                auto BeginLine = SM.getSpellingLineNumber(BeginLoc);
                auto BeginCol = SM.getSpellingColumnNumber(BeginLoc);
                auto EndLine = SM.getSpellingLineNumber(EndLoc);
                auto EndCol = SM.getSpellingColumnNumber(EndLoc);
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

        return AlignedASTNodes;
    }
} // namespace cpp2c
