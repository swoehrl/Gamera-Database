#include "ExecutionVisitor.hpp"
#include "harriet/Expression.hpp"
#include "query/operator/PrintOperator.hpp"
#include "query/operator/RootOperator.hpp"
#include "query/result/QueryResultCollection.hpp"
#include "schema/RelationSchema.hpp"
#include "schema/SchemaManager.hpp"
#include "segment_manager/SegmentManager.hpp"
#include "Statement.hpp"
#include "util/Utility.hpp"
#include "segment_manager/SPSegment.hpp"
#include <sstream>

using namespace std;

namespace dbi {

namespace script {

ExecutionVisitor::ExecutionVisitor(SegmentManager& segmentManager, SchemaManager& schemaManager, QueryResultCollection& result)
: segmentManager(segmentManager)
, schemaManager(schemaManager)
, result(result)
{
}

ExecutionVisitor::~ExecutionVisitor()
{
}

void ExecutionVisitor::onPreVisit(SelectStatement& select)
{
   select.queryPlan->checkTypes();
   select.queryPlan->execute();
   result.addSelect(select.queryPlan->getExecutionTime(), select.queryPlan->getResult(), select.queryPlan->getSuppliedColumns());
}

void ExecutionVisitor::onPreVisit(CreateTableStatement& createTable)
{
   // Create attributes
   vector<ColumnSchema> columns;
   for(auto& iter : createTable.attributes)
      columns.push_back(dbi::ColumnSchema{iter.name, iter.type, iter.notNull, 0});

   // Create indexes
   vector<IndexSchema> indexes(createTable.uniqueColumns.size());
   for(uint32_t k=0; k<createTable.uniqueColumns.size(); k++) {
      indexes[k].unique = true;
      indexes[k].sid = segmentManager.createSegment(SegmentType::BT, 1);
      indexes[k].type = IndexSchema::Type::kBTree;

      for(auto& columnName : createTable.uniqueColumns[k]) {
         for(uint32_t i=0; i<columns.size(); i++) {
            if(columnName == columns[i].name) {
               indexes[k].indexedColumns.push_back(i);
               break;
            }
         }
      }
   }

   // Add relation
   auto schema = util::make_unique<RelationSchema>(createTable.tableName, move(columns), move(indexes));
   SegmentId sid = segmentManager.createSegment(SegmentType::SP, kInitialPagesPerRelation);
   schema->setSegmentId(sid);
   schema->optimizePadding();
   schemaManager.addRelation(move(schema));
   result.addCreate(chrono::nanoseconds(-1), createTable.tableName);
}

void ExecutionVisitor::onPreVisit(InsertStatement& insert)
{
   // Get target relation
   auto& targetSchema = schemaManager.getRelation(insert.tableName);
   SPSegment& targetSegment = segmentManager.getSPSegment(targetSchema.getRelationSegmentId());

   // Do the insert
   targetSegment.insert(targetSchema.tupleToRecord(insert.values));

   result.addInsert(chrono::nanoseconds(-1), insert.tableName);
}

void ExecutionVisitor::onPreVisit(DropTableStatement& dropTable)
{
   auto& schema = schemaManager.getRelation(dropTable.tableName);
   segmentManager.dropSegmentById(schema.getRelationSegmentId());
   for(auto& index : schema.getIndexes())
      segmentManager.dropSegmentById(index.sid);
   schemaManager.dropRelation(dropTable.tableName);
}

}

}
