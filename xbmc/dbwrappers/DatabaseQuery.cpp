/*
 *      Copyright (C) 2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "DatabaseQuery.h"
#include "Database.h"
#include "XBDateTime.h"
#include "guilib/LocalizeStrings.h"
#include "utils/CharsetConverter.h"
#include "utils/StringUtils.h"
#include "utils/Variant.h"
#include "utils/XBMCTinyXML.h"

using namespace std;

typedef struct
{
  char string[15];
  CDatabaseQueryRule::SEARCH_OPERATOR op;
  int localizedString;
} operatorField;

static const operatorField operators[] = {
  { "contains",        CDatabaseQueryRule::OPERATOR_CONTAINS,          21400 },
  { "doesnotcontain",  CDatabaseQueryRule::OPERATOR_DOES_NOT_CONTAIN,  21401 },
  { "is",              CDatabaseQueryRule::OPERATOR_EQUALS,            21402 },
  { "isnot",           CDatabaseQueryRule::OPERATOR_DOES_NOT_EQUAL,    21403 },
  { "startswith",      CDatabaseQueryRule::OPERATOR_STARTS_WITH,       21404 },
  { "endswith",        CDatabaseQueryRule::OPERATOR_ENDS_WITH,         21405 },
  { "greaterthan",     CDatabaseQueryRule::OPERATOR_GREATER_THAN,      21406 },
  { "lessthan",        CDatabaseQueryRule::OPERATOR_LESS_THAN,         21407 },
  { "after",           CDatabaseQueryRule::OPERATOR_AFTER,             21408 },
  { "before",          CDatabaseQueryRule::OPERATOR_BEFORE,            21409 },
  { "inthelast",       CDatabaseQueryRule::OPERATOR_IN_THE_LAST,       21410 },
  { "notinthelast",    CDatabaseQueryRule::OPERATOR_NOT_IN_THE_LAST,   21411 },
  { "true",            CDatabaseQueryRule::OPERATOR_TRUE,              20122 },
  { "false",           CDatabaseQueryRule::OPERATOR_FALSE,             20424 },
  { "between",         CDatabaseQueryRule::OPERATOR_BETWEEN,           21456 }
};

static const size_t NUM_OPERATORS = sizeof(operators) / sizeof(operatorField);

#define RULE_VALUE_SEPARATOR  " / "

CDatabaseQueryRule::CDatabaseQueryRule()
{
  m_field = 0;
  m_operator = OPERATOR_CONTAINS;
}

bool CDatabaseQueryRule::Load(const TiXmlNode *node, const std::string &encoding /* = "UTF-8" */)
{
  if (node == NULL)
    return false;

  const TiXmlElement *element = node->ToElement();
  if (element == NULL)
    return false;

  // format is:
  // <rule field="Genre" operator="contains">parameter</rule>
  // where parameter can either be a string or a list of
  // <value> tags containing a string
  const char *field = element->Attribute("field");
  const char *oper = element->Attribute("operator");
  if (field == NULL || oper == NULL)
    return false;

  m_field = TranslateField(field);
  m_operator = TranslateOperator(oper);

  if (m_operator == OPERATOR_TRUE || m_operator == OPERATOR_FALSE)
    return true;

  const TiXmlNode *parameter = element->FirstChild();
  if (parameter == NULL)
    return false;

  if (parameter->Type() == TiXmlNode::TINYXML_TEXT)
  {
    CStdString utf8Parameter;
    if (encoding.empty()) // utf8
      utf8Parameter = parameter->ValueStr();
    else
      g_charsetConverter.ToUtf8(encoding, parameter->ValueStr(), utf8Parameter);

    if (!utf8Parameter.empty())
      m_parameter.push_back(utf8Parameter);
  }
  else if (parameter->Type() == TiXmlNode::TINYXML_ELEMENT)
  {
    const TiXmlNode *valueNode = element->FirstChild("value");
    while (valueNode != NULL)
    {
      const TiXmlNode *value = valueNode->FirstChild();
      if (value != NULL && value->Type() == TiXmlNode::TINYXML_TEXT)
      {
        CStdString utf8Parameter;
        if (encoding.empty()) // utf8
          utf8Parameter = value->ValueStr();
        else
          g_charsetConverter.ToUtf8(encoding, value->ValueStr(), utf8Parameter);

        if (!utf8Parameter.empty())
          m_parameter.push_back(utf8Parameter);
      }

      valueNode = valueNode->NextSibling("value");
    }
  }
  else
    return false;

  return true;
}

bool CDatabaseQueryRule::Load(const CVariant &obj)
{
  if (!obj.isObject() ||
      !obj.isMember("field") || !obj["field"].isString() ||
      !obj.isMember("operator") || !obj["operator"].isString())
    return false;

  m_field = TranslateField(obj["field"].asString().c_str());
  m_operator = TranslateOperator(obj["operator"].asString().c_str());

  if (m_operator == OPERATOR_TRUE || m_operator == OPERATOR_FALSE)
    return true;

  if (!obj.isMember("value") || (!obj["value"].isString() && !obj["value"].isArray()))
    return false;

  const CVariant &value = obj["value"];
  if (value.isString() && !value.asString().empty())
    m_parameter.push_back(value.asString());
  else if (value.isArray())
  {
    for (CVariant::const_iterator_array val = value.begin_array(); val != value.end_array(); val++)
    {
      if (val->isString() && !val->asString().empty())
        m_parameter.push_back(val->asString());
    }
  }
  else
    return false;

  return true;
}

bool CDatabaseQueryRule::Save(TiXmlNode *parent) const
{
  if (parent == NULL || (m_parameter.empty() && m_operator != OPERATOR_TRUE && m_operator != OPERATOR_FALSE))
    return false;

  TiXmlElement rule("rule");
  rule.SetAttribute("field", TranslateField(m_field).c_str());
  rule.SetAttribute("operator", TranslateOperator(m_operator).c_str());

  for (vector<CStdString>::const_iterator it = m_parameter.begin(); it != m_parameter.end(); it++)
  {
    TiXmlElement value("value");
    TiXmlText text(it->c_str());
    value.InsertEndChild(text);
    rule.InsertEndChild(value);
  }

  parent->InsertEndChild(rule);

  return true;
}

bool CDatabaseQueryRule::Save(CVariant &obj) const
{
  if (obj.isNull() || (m_parameter.empty() && m_operator != OPERATOR_TRUE && m_operator != OPERATOR_FALSE))
    return false;

  obj["field"] = TranslateField(m_field);
  obj["operator"] = TranslateOperator(m_operator);

  obj["value"] = CVariant(CVariant::VariantTypeArray);
  for (vector<CStdString>::const_iterator it = m_parameter.begin(); it != m_parameter.end(); it++)
    obj["value"].push_back(*it);

  return true;
}

CDatabaseQueryRule::SEARCH_OPERATOR CDatabaseQueryRule::TranslateOperator(const char *oper)
{
  for (unsigned int i = 0; i < NUM_OPERATORS; i++)
    if (StringUtils::EqualsNoCase(oper, operators[i].string)) return operators[i].op;
  return OPERATOR_CONTAINS;
}

CStdString CDatabaseQueryRule::TranslateOperator(SEARCH_OPERATOR oper)
{
  for (unsigned int i = 0; i < NUM_OPERATORS; i++)
    if (oper == operators[i].op) return operators[i].string;
  return "contains";
}

CStdString CDatabaseQueryRule::GetLocalizedOperator(SEARCH_OPERATOR oper)
{
  for (unsigned int i = 0; i < NUM_OPERATORS; i++)
    if (oper == operators[i].op) return g_localizeStrings.Get(operators[i].localizedString);
  return g_localizeStrings.Get(16018);
}

void CDatabaseQueryRule::GetAvailableOperators(std::vector<std::string> &operatorList)
{
  for (unsigned int index = 0; index < NUM_OPERATORS; index++)
    operatorList.push_back(operators[index].string);
}

CStdString CDatabaseQueryRule::GetParameter() const
{
  return StringUtils::JoinString(m_parameter, RULE_VALUE_SEPARATOR);
}

void CDatabaseQueryRule::SetParameter(const CStdString &value)
{
  m_parameter.clear();
  StringUtils::SplitString(value, RULE_VALUE_SEPARATOR, m_parameter);
}

void CDatabaseQueryRule::SetParameter(const std::vector<CStdString> &values)
{
  m_parameter.assign(values.begin(), values.end());
}

CStdString CDatabaseQueryRule::FormatParameter(const CStdString &operatorString, const CStdString &param, const CDatabase &db, const CStdString &strType) const
{
  CStdString parameter;
  if (GetFieldType(m_field) == TEXTIN_FIELD)
  {
    CStdStringArray split;
    StringUtils::SplitString(param, ",", split);
    for (CStdStringArray::iterator itIn = split.begin(); itIn != split.end(); ++itIn)
    {
      if (!parameter.empty())
        parameter += ",";
      parameter += db.PrepareSQL("'%s'", (*itIn).Trim().c_str());
    }
    parameter = " IN (" + parameter + ")";
  }
  else
    parameter = db.PrepareSQL(operatorString.c_str(), param.c_str());

  if (GetFieldType(m_field) == DATE_FIELD)
  {
    if (m_operator == OPERATOR_IN_THE_LAST || m_operator == OPERATOR_NOT_IN_THE_LAST)
    { // translate time period
      CDateTime date=CDateTime::GetCurrentDateTime();
      CDateTimeSpan span;
      span.SetFromPeriod(param);
      date-=span;
      parameter = db.PrepareSQL(operatorString.c_str(), date.GetAsDBDate().c_str());
    }
  }
  return parameter;
}

CStdString CDatabaseQueryRule::GetOperatorString(SEARCH_OPERATOR op) const
{
  CStdString operatorString;
  if (GetFieldType(m_field) != TEXTIN_FIELD)
  {
    // the comparison piece
    switch (op)
    {
    case OPERATOR_CONTAINS:
      operatorString = " LIKE '%%%s%%'"; break;
    case OPERATOR_DOES_NOT_CONTAIN:
      operatorString = " LIKE '%%%s%%'"; break;
    case OPERATOR_EQUALS:
      if (GetFieldType(m_field) == NUMERIC_FIELD || GetFieldType(m_field) == SECONDS_FIELD)
        operatorString = " = %s";
      else
        operatorString = " LIKE '%s'";
      break;
    case OPERATOR_DOES_NOT_EQUAL:
      if (GetFieldType(m_field) == NUMERIC_FIELD || GetFieldType(m_field) == SECONDS_FIELD)
        operatorString = " != %s";
      else
        operatorString = " LIKE '%s'";
      break;
    case OPERATOR_STARTS_WITH:
      operatorString = " LIKE '%s%%'"; break;
    case OPERATOR_ENDS_WITH:
      operatorString = " LIKE '%%%s'"; break;
    case OPERATOR_AFTER:
    case OPERATOR_GREATER_THAN:
    case OPERATOR_IN_THE_LAST:
      operatorString = " > ";
      if (GetFieldType(m_field) == NUMERIC_FIELD || GetFieldType(m_field) == SECONDS_FIELD)
        operatorString += "%s";
      else
        operatorString += "'%s'";
      break;
    case OPERATOR_BEFORE:
    case OPERATOR_LESS_THAN:
    case OPERATOR_NOT_IN_THE_LAST:
      operatorString = " < ";
      if (GetFieldType(m_field) == NUMERIC_FIELD || GetFieldType(m_field) == SECONDS_FIELD)
        operatorString += "%s";
      else
        operatorString += "'%s'";
      break;
    case OPERATOR_TRUE:
      operatorString = " = 1"; break;
    case OPERATOR_FALSE:
      operatorString = " = 0"; break;
    default:
      break;
    }
  }
  return operatorString;
}

CStdString CDatabaseQueryRule::GetWhereClause(const CDatabase &db, const CStdString& strType) const
{
  SEARCH_OPERATOR op = GetOperator(strType);

  CStdString operatorString = GetOperatorString(op);
  CStdString negate;
  if (op == OPERATOR_DOES_NOT_CONTAIN || op == OPERATOR_FALSE ||
     (op == OPERATOR_DOES_NOT_EQUAL && GetFieldType(m_field) != NUMERIC_FIELD && GetFieldType(m_field) != SECONDS_FIELD))
    negate = " NOT";

  // boolean operators don't have any values in m_parameter, they work on the operator
  if (m_operator == OPERATOR_FALSE || m_operator == OPERATOR_TRUE)
    return GetBooleanQuery(negate, strType);

  // The BETWEEN operator is handled special
  if (op == OPERATOR_BETWEEN)
  {
    if (m_parameter.size() != 2)
      return "";

    FIELD_TYPE fieldType = GetFieldType(m_field);
    if (fieldType == NUMERIC_FIELD)
      return db.PrepareSQL("CAST(%s as DECIMAL(5,1)) BETWEEN %s AND %s", GetField(m_field, strType).c_str(), m_parameter[0].c_str(), m_parameter[1].c_str());
    else if (fieldType == SECONDS_FIELD)
      return db.PrepareSQL("CAST(%s as INTEGER) BETWEEN %s AND %s", GetField(m_field, strType).c_str(), m_parameter[0].c_str(), m_parameter[1].c_str());
    else
      return db.PrepareSQL("%s BETWEEN '%s' AND '%s'", GetField(m_field, strType).c_str(), m_parameter[0].c_str(), m_parameter[1].c_str());
  }

  // now the query parameter
  CStdString wholeQuery;
  for (vector<CStdString>::const_iterator it = m_parameter.begin(); it != m_parameter.end(); ++it)
  {
    CStdString query = "(" + FormatWhereClause(negate, operatorString, *it, db, strType) + ")";

    if (it+1 != m_parameter.end())
      query += " OR ";

    wholeQuery += query;
  }

  return wholeQuery;
}

CStdString CDatabaseQueryRule::FormatWhereClause(const CStdString &negate, const CStdString &oper, const CStdString &param,
                                                 const CDatabase &db, const CStdString &strType) const
{
  CStdString parameter = FormatParameter(oper, param, db, strType);

  CStdString query;
  if (m_field != 0)
  {
    string fmt = "%s";
    if (GetFieldType(m_field) == NUMERIC_FIELD)
      fmt = "CAST(%s as DECIMAL(5,1))";
    else if (GetFieldType(m_field) == SECONDS_FIELD)
      fmt = "CAST(%s as INTEGER)";

    query = StringUtils::Format(fmt.c_str(), GetField(m_field,strType).c_str());
    query += negate + parameter;
  }

  if (query.Equals(negate + parameter))
    query = "1";
  return query;
}

CDatabaseQueryRuleCombination::CDatabaseQueryRuleCombination()
  : m_type(CombinationAnd)
{ }

void CDatabaseQueryRuleCombination::clear()
{
  m_combinations.clear();
  m_rules.clear();
  m_type = CombinationAnd;
}

CStdString CDatabaseQueryRuleCombination::GetWhereClause(const CDatabase &db, const CStdString& strType) const
{
  CStdString rule, currentRule;

  // translate the combinations into SQL
  for (CDatabaseQueryRuleCombinations::const_iterator it = m_combinations.begin(); it != m_combinations.end(); ++it)
  {
    if (it != m_combinations.begin())
      rule += m_type == CombinationAnd ? " AND " : " OR ";
    rule += "(" + (*it)->GetWhereClause(db, strType) + ")";
  }

  // translate the rules into SQL
  for (CDatabaseQueryRules::const_iterator it = m_rules.begin(); it != m_rules.end(); ++it)
  {
    if (!rule.empty())
      rule += m_type == CombinationAnd ? " AND " : " OR ";
    rule += "(";
    CStdString currentRule = (*it)->GetWhereClause(db, strType);
    // if we don't get a rule, we add '1' or '0' so the query is still valid and doesn't fail
    if (currentRule.empty())
      currentRule = m_type == CombinationAnd ? "'1'" : "'0'";
    rule += currentRule;
    rule += ")";
  }

  return rule;
}

bool CDatabaseQueryRuleCombination::Load(const CVariant &obj, const IDatabaseQueryRuleFactory *factory)
{
  if (!obj.isObject() && !obj.isArray())
    return false;
  
  CVariant child;
  if (obj.isObject())
  {
    if (obj.isMember("and") && obj["and"].isArray())
    {
      m_type = CombinationAnd;
      child = obj["and"];
    }
    else if (obj.isMember("or") && obj["or"].isArray())
    {
      m_type = CombinationOr;
      child = obj["or"];
    }
    else
      return false;
  }
  else
    child = obj;

  for (CVariant::const_iterator_array it = child.begin_array(); it != child.end_array(); it++)
  {
    if (!it->isObject())
      continue;

    if (it->isMember("and") || it->isMember("or"))
    {
      boost::shared_ptr<CDatabaseQueryRuleCombination> combo(factory->CreateCombination());
      if (combo && combo->Load(*it, factory))
        m_combinations.push_back(combo);
    }
    else
    {
      boost::shared_ptr<CDatabaseQueryRule> rule(factory->CreateRule());
      if (rule && rule->Load(*it))
        m_rules.push_back(rule);
    }
  }

  return true;
}

bool CDatabaseQueryRuleCombination::Save(TiXmlNode *parent) const
{
  for (CDatabaseQueryRules::const_iterator it = m_rules.begin(); it != m_rules.end(); ++it)
    (*it)->Save(parent);
  return true;
}

bool CDatabaseQueryRuleCombination::Save(CVariant &obj) const
{
  if (!obj.isObject() || (m_combinations.empty() && m_rules.empty()))
    return false;

  CVariant comboArray(CVariant::VariantTypeArray);
  if (!m_combinations.empty())
  {
    for (CDatabaseQueryRuleCombinations::const_iterator combo = m_combinations.begin(); combo != m_combinations.end(); combo++)
    {
      CVariant comboObj(CVariant::VariantTypeObject);
      if ((*combo)->Save(comboObj))
        comboArray.push_back(comboObj);
    }

  }
  if (!m_rules.empty())
  {
    for (CDatabaseQueryRules::const_iterator rule = m_rules.begin(); rule != m_rules.end(); rule++)
    {
      CVariant ruleObj(CVariant::VariantTypeObject);
      if ((*rule)->Save(ruleObj))
        comboArray.push_back(ruleObj);
    }
  }

  obj[TranslateCombinationType()] = comboArray;

  return true;
}

std::string CDatabaseQueryRuleCombination::TranslateCombinationType() const
{
  return m_type == CombinationAnd ? "and" : "or";
}
