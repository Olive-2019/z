#ifndef PARSER_SYNTAXTREE_H
#define PARSER_SYNTAXTREE_H

#include <queue>
#include "parser_lexer.h"
#include "parser_node.h"
#include "parser_token.h"

class SyntaxTree {
public:
	SyntaxTree(LexerPtr& lexer)
		: lexer_(lexer)
	{}
public:
	void resetParser(string& stream)
	{
		reset_parser();
		lexer_->setStream(stream);
		tokens_.clear();
	}
	NODE* buildSyntaxTree();
private:
	bool parseCommand(NODE* &);
	bool parseUtility(NODE* &);
	bool parseDDL(NODE* &);
	bool parseDML(NODE* &);
	bool parseCreateTable(NODE* &);
	bool parseCreateDatabase(NODE*&);
	bool parseUseDatabase(NODE* &);
	bool parseCreateIndex(NODE* &);
	bool parseInsert(NODE*&);
	bool parseDelete(NODE*&);
	bool parseUpdate(NODE* &);
	bool parseQuery(NODE* &);
	bool parseDropTable(NODE* &);
	bool parseDropIndex(NODE* &);
	bool parserShowDistribution(NODE* &);
	bool parserScript(NODE* &);

	bool parseNonmtAttrtypeList(NODE* &);
	void parseNonmtSelectClause(NODE* &);
	void parseNonmtAggrelattrList(NODE* &);
	void parseNonmtRelationList(NODE* &);
	void parseNonmtCondList(NODE* &);
	bool parseAggrelattr(NODE* &);
	bool parseNonmtValueList(NODE* &);
	bool parseValue(NODE* &);
	bool parseAttrtype(NODE* &);
	bool parseRelAttr(NODE* &);
	bool parseUpdateNewValue(NODE*&);

	void parseOptWhereClause(NODE* &);
	void parseOptOrderByClause(NODE* &);
	void parseOptGroupByClause(NODE* &);
	void supply_relation(NODE*, NODE* rel);
	void supply_cond_relation(NODE* cond, NODE* rel);
	void parseCondition(NODE* &);
	void parseRelAttrOrValue(NODE* &);
private:
	TokenPtr next();
	TokenPtr peek(int pos);
	void discard(int num);
private:
	LexerPtr lexer_;
	deque<TokenPtr> tokens_; /* ���ڼ�¼Token */
};

#endif /* PARSER_SYNTEXTREE_H */
