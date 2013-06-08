#include "TransactionCallbackHandler.hpp"
#include "schema/RelationSchema.hpp"
#include "schema/SchemaManager.hpp"
#include "segment_manager/SegmentManager.hpp"
#include "common/Config.hpp"
#include <iostream>

using namespace std;

namespace dbi {

TransactionCallbackHandler::TransactionCallbackHandler(SegmentManager& segmentManager, SchemaManager& schemaManager)
: segmentManager(segmentManager)
, schemaManager(schemaManager)
{
}

void TransactionCallbackHandler::createTable(RelationSchema& schema)
{
   schema.setSegmentId(segmentManager.createSegment(SegmentType::SP, kInitialPagesPerRelation));
   schemaManager.addRelation(schema);
}

void TransactionCallbackHandler::insertIntoTable(string& tableName, vector<unique_ptr<harriet::Value>>&)
{
   RelationSchema& relationSchema = schemaManager.getRelation(tableName);

   cout << "inserting into " << relationSchema.getName() << endl;
}

}
