/* This file is part of KDevelop
    Copyright 2007 David Nolden <david.nolden.kdevelop@art-master.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "expressionparser.h"

#include <language/duchain/duchain.h>
#include "cppduchain/usebuilder.h"
#include "cppduchain/declarationbuilder.h"
#include "cppduchain/dumpchain.h"
#include "cppduchain/dumptypes.h"
#include <language/duchain/declaration.h>

#include <language/duchain/ducontext.h>
#include "ast.h"
#include "parsesession.h"
#include "parser.h"
#include "control.h"
#include <language/duchain/duchainlock.h>
#include <language/duchain/identifier.h>
#include "expressionvisitor.h"
#include <parser/rpp/chartools.h>


namespace Cpp {
using namespace KDevelop;

ExpressionParser::ExpressionParser( bool strict, bool debug ) : m_strict(strict), m_debug(debug) {
}

ExpressionEvaluationResult ExpressionParser::evaluateType( const QByteArray& unit, DUContextPointer context, const KDevelop::TopDUContext* source, bool forceExpression ) {

  if( m_debug )
    kDebug(9007) << "==== .Evaluating ..:" << endl << unit;

  ParseSession* session = new ParseSession();

  Control control;
  DumpChain dumper;

  Parser parser(&control);

  AST* ast = 0;

  DUContext::ContextType type;
  {
    DUChainReadLocker lock(DUChain::lock());
    if( !context )
      return ExpressionEvaluationResult();
    type = context->type();
  }

  session->setContentsAndGenerateLocationTable(tokenizeFromByteArray(unit));

  ast = parser.parseTypeOrExpression(session, forceExpression);

  if(!ast) {
    kDebug(9007) << "Failed to parse \"" << unit << "\"";
    delete session;
    return ExpressionEvaluationResult();
  }
  
  if (m_debug) {
    kDebug(9007) << "===== AST:";
    dumper.dump(ast, session);
  }

  ast->ducontext = context.data();

  if(!ast->ducontext) {
    kDebug() << "context disappeared";
    return ExpressionEvaluationResult();
  }
  
  ///@todo think how useful it is to compute contexts and uses here. The main thing we need is the AST.
  /*
  static int testNumber = 0; //@todo what this url for?
  KUrl url(QString("file:///internal/evaluate_%1").arg(testNumber++));
  kDebug(9007) << "url:" << url;

  DeclarationBuilder definitionBuilder(session);
  DUContext* top = definitionBuilder.buildSubDeclarations(url, ast, context);

  UseBuilder useBuilder(session);
  useBuilder.buildUses(ast);

  if (m_debug) {
    kDebug(9007) << "===== DUChain:";

    DUChainReadLocker lock(DUChain::lock());
    dumper.dump(top, false);
  }

  if (m_debug) {
    kDebug(9007) << "===== Types:";
    DumpTypes dt;
    DUChainReadLocker lock(DUChain::lock());
    foreach (const AbstractType::Ptr& type, definitionBuilder.topTypes())
      dt.dump(type.data());
  }

  if (m_debug)
    kDebug(9007) << "===== Finished evaluation.";
  */
  ExpressionEvaluationResult ret = evaluateType( ast, session, source );

  delete session;

  /*
  {
    DUChainReadLocker lock(DUChain::lock());
    delete top;
  }*/

  return ret;
}

ExpressionEvaluationResult ExpressionParser::evaluateExpression( const QByteArray& expression, DUContextPointer context, const KDevelop::TopDUContext* source ) {
  return evaluateType( expression, context, source, true );
}

ExpressionEvaluationResult ExpressionParser::evaluateType( AST* ast, ParseSession* session, const KDevelop::TopDUContext* source ) {
  
  if (m_debug) {
    DumpChain dumper;
    kDebug(9007) << "===== AST:";
    dumper.dump(ast, session);
  }
  
  ExpressionEvaluationResult ret;
  ExpressionVisitor v(session, source, m_strict);
  v.parse( ast );

  DUChainReadLocker lock(DUChain::lock());
  
  ret.type = v.lastType()->indexed();
  ret.isInstance = v.lastInstance().isInstance;
  
  if(v.lastInstance().declaration)
    ret.instanceDeclaration = KDevelop::DeclarationId(KDevelop::IndexedDeclaration(v.lastInstance().declaration.data()));
  
  foreach(const DeclarationPointer &decl, v.lastDeclarations())
    if(decl)
      ret.allDeclarations.append(decl->id());
  return ret;
}

}
