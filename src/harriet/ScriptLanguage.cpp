#include "ScriptLanguage.hpp"
#include "Utility.hpp"
#include "Expression.hpp"
#include "Environment.hpp"
#include "Function.hpp"
#include <sstream>
#include <ctype.h>
#include <cassert>
#include <istream>
#include <algorithm>
#include <cstring>
//---------------------------------------------------------------------------
// Harriet Script Language
// Copyright (c) 2013 Alexander van Renen (alexandervanrenen@gmail.com)
// See the file LICENSE.txt for copying permission.
//---------------------------------------------------------------------------
using namespace std;
//---------------------------------------------------------------------------
namespace harriet {
//---------------------------------------------------------------------------
VariableType nameToType(const string& name) throw(Exception)
{
   if(name == kVariableInteger)
      return VariableType::TInteger;
   if(name == kVariableFloat)
      return VariableType::TFloat;
   if(name == kVariableBool)
      return VariableType::TBool;
   if(name == kVariableVector)
      return VariableType::TVector;
   if(name == kVariableCharacter)
      return VariableType::TCharacter;
   throw Exception{"invalid type name: " + name};
}
//---------------------------------------------------------------------------
const string typeToName(VariableType type) throw()
{
   switch(type) {
      case VariableType::TInteger:
         return kVariableInteger;
      case VariableType::TFloat:
         return kVariableFloat;
      case VariableType::TBool:
         return kVariableBool;
      case VariableType::TVector:
         return kVariableVector;
      case VariableType::TCharacter:
         return kVariableCharacter;
   }
   throw Exception{"unreachable"};
}
//---------------------------------------------------------------------------
uint16_t getDefaultLengthOfBinary(VariableType type)
{
   switch(type) {
      case VariableType::TInteger:
         return IntegerValue(0).typeSize();
      case VariableType::TFloat:
         return FloatValue(.0f).typeSize();
      case VariableType::TBool:
         return BoolValue(true).typeSize();
      case VariableType::TVector:
         return VectorValue(Vector3<float>(0,0,0)).typeSize();
      case VariableType::TCharacter:
         return CharacterValue(vector<char>(1, '\0'), 1).typeSize();
   }
   throw Exception{"unreachable"};
}
//---------------------------------------------------------------------------
unique_ptr<Value> createDefaultValue(VariableType type) throw()
{
   switch(type) {
      case VariableType::TInteger:
         return make_unique<IntegerValue>(0);
      case VariableType::TFloat:
         return make_unique<FloatValue>(.0f);
      case VariableType::TBool:
         return make_unique<BoolValue>(true);
      case VariableType::TVector:
         return make_unique<VectorValue>(Vector3<float>(0,0,0));
      case VariableType::TCharacter:
         return make_unique<CharacterValue>(vector<char>(1, '\0'), 1);
   }
   throw Exception{"unreachable"};
}
//---------------------------------------------------------------------------
unique_ptr<Value> readValueContent(VariableType type, uint16_t length, const char* data) throw()
{
   switch(type) {
      case VariableType::TInteger:
         return make_unique<IntegerValue>(*reinterpret_cast<const int32_t*>(data));
      case VariableType::TFloat:
         return make_unique<FloatValue>(*reinterpret_cast<const float*>(data));
      case VariableType::TBool:
         return make_unique<BoolValue>(*reinterpret_cast<const bool*>(data));
      case VariableType::TVector:
         return make_unique<VectorValue>(*reinterpret_cast<const Vector3<float>*>(data));
      case VariableType::TCharacter: {
         vector<char> result(length);
         memcpy(result.data(), data, length);
         return make_unique<CharacterValue>(result, length);
      }
   }
   throw Exception{"unreachable"};
}
//---------------------------------------------------------------------------
void writeValueContent(const Value& value, char* data) throw()
{
   switch(value.getResultType()) {
      case VariableType::TInteger:
         memcpy(data, &reinterpret_cast<const IntegerValue&>(value).result, sizeof(int32_t));
         return;
      case VariableType::TFloat:
         memcpy(data, &reinterpret_cast<const FloatValue&>(value).result, sizeof(float));
         return;
      case VariableType::TBool:
         memcpy(data, &reinterpret_cast<const BoolValue&>(value).result, sizeof(bool));
         return;
      case VariableType::TVector:
         memcpy(data, &reinterpret_cast<const VectorValue&>(value).result, sizeof(Vector3<float>));
         return;
      case VariableType::TCharacter:
         memcpy(data, reinterpret_cast<const CharacterValue&>(value).result.data(), reinterpret_cast<const CharacterValue&>(value).result.size());
         return;
   }
   throw Exception{"unreachable"};
}
//---------------------------------------------------------------------------
bool isImplicitCastPossible(VariableType from, VariableType to) throw()
{
   bool implicitCast[5][5] = {
      /* to\from         int     float   bool    vector  character */
      /* int       */ {  true ,  true ,  false,  false,  false},
      /* float     */ {  true ,  true ,  false,  false,  false},
      /* bool      */ {  false,  false,  true ,  false,  false},
      /* vector    */ {  false,  false,  false,  true ,  false},
      /* charactor */ {  false,  false,  false,  false,  true }
   };

   return implicitCast[static_cast<uint32_t>(to)][static_cast<uint32_t>(from)];
}
//---------------------------------------------------------------------------
unique_ptr<Expression> createCast(unique_ptr<Expression> expression, harriet::VariableType resultType)
{
   unique_ptr<CastOperator> result;
   switch(resultType) {
      case harriet::VariableType::TInteger: result = make_unique<IntegerCast>(); break;
      case harriet::VariableType::TFloat:   result = make_unique<FloatCast>();   break;
      case harriet::VariableType::TBool:    result = make_unique<BoolCast>();    break;
      case harriet::VariableType::TVector:  result = make_unique<VectorCast>();  break;
      default:                                     throw Exception{"unable to cast to: " + typeToName(resultType)};
   }
   result->addChild(::move(expression));
   return ::move(result);
}
//---------------------------------------------------------------------------
} // end of namespace harriet
//---------------------------------------------------------------------------
