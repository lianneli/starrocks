// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.starrocks.sql.common;

import com.starrocks.sql.optimizer.operator.Operator;
import com.starrocks.sql.optimizer.operator.OperatorVisitor;
import com.starrocks.sql.optimizer.operator.logical.LogicalAggregationOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalAssertOneRowOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalCTEAnchorOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalCTEConsumeOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalCTEProduceOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalEsScanOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalExceptOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalFilterOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalHiveScanOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalHudiScanOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalIcebergScanOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalIntersectOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalJDBCScanOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalJoinOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalLimitOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalMetaScanOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalMysqlScanOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalOlapScanOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalProjectOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalRepeatOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalScanOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalSchemaScanOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalTableFunctionOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalTopNOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalUnionOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalValuesOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalViewScanOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalWindowOperator;
import com.starrocks.sql.optimizer.operator.logical.MockOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalAssertOneRowOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalCTEAnchorOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalCTEConsumeOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalCTEProduceOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalDistributionOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalEsScanOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalExceptOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalFilterOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalHashAggregateOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalHashJoinOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalHiveScanOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalHudiScanOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalIcebergScanOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalIntersectOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalJDBCScanOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalLimitOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalMetaScanOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalMysqlScanOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalNestLoopJoinOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalNoCTEOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalOlapScanOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalProjectOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalRepeatOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalSchemaScanOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalTableFunctionOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalTopNOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalUnionOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalValuesOperator;
import com.starrocks.sql.optimizer.operator.physical.PhysicalWindowOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import org.jetbrains.annotations.NotNull;

import java.util.ArrayList;
import java.util.List;
import java.util.stream.Collectors;

public class DebugOperatorTracer extends OperatorVisitor<String, Void> {
    @Override
    public String visitOperator(Operator op, Void context) {
        return op.toString();
    }

    @Override
    public String visitLogicalTableScan(LogicalScanOperator node, Void context) {
        return "LogicalScanOperator" + " {" +
                "table='" + node.getTable().getId() + '\'' +
                ", outputColumns='" + new ArrayList<>(node.getColRefToColumnMetaMap().keySet()) + '\'' +
                '}';
    }

    @Override
    public String visitLogicalViewScan(LogicalViewScanOperator node, Void context) {
        return "LogicalViewScanOperator" + " {" +
                "table='" + node.getTable().getName() + '\'' +
                ", outputColumns='" + new ArrayList<>(node.getColRefToColumnMetaMap().keySet()) + '\'' +
                '}';
    }

    @Override
    public String visitLogicalSchemaScan(LogicalSchemaScanOperator node, Void context) {
        return super.visitLogicalSchemaScan(node, context);
    }

    @Override
    public String visitLogicalOlapScan(LogicalOlapScanOperator node, Void context) {
        return "LogicalOlapScanOperator" + " {" + "table=" + node.getTable().getId() +
                ", selectedPartitionId=" + node.getSelectedPartitionId() +
                ", selectedIndexId=" + node.getSelectedIndexId() +
                ", outputColumns=" + new ArrayList<>(node.getColRefToColumnMetaMap().keySet()) +
                ", predicate=" + node.getPredicate() +
                ", prunedPartitionPredicates=" + node.getPrunedPartitionPredicates() +
                ", limit=" + node.getLimit() +
                "}";
    }

    @Override
    public String visitLogicalHiveScan(LogicalHiveScanOperator node, Void context) {
        return "LogicalHiveScanOperator" + " {" + "table=" + node.getTable().getCatalogTableName() +
                ", outputColumns=" + new ArrayList<>(node.getColRefToColumnMetaMap().keySet()) +
                ", predicates=" + node.getScanOperatorPredicates() +
                ", limit=" + node.getLimit() +
                "}";
    }

    @Override
    public String visitLogicalIcebergScan(LogicalIcebergScanOperator node, Void context) {
        StringBuilder sb = new StringBuilder("LogicalIcebergScanOperator");
        sb.append(" {").append("table=").append(node.getTable().getCatalogTableName())
                .append(", outputColumns=").append(new ArrayList<>(node.getColRefToColumnMetaMap().keySet()))
                .append(", predicates=").append(node.getScanOperatorPredicates())
                .append("}");
        return sb.toString();
    }

    @Override
    public String visitLogicalHudiScan(LogicalHudiScanOperator node, Void context) {
        return "LogicalHudiScanOperator" + " {" + "table=" + node.getTable().getCatalogTableName() +
                ", outputColumns=" + new ArrayList<>(node.getColRefToColumnMetaMap().keySet()) +
                ", predicates=" + node.getScanOperatorPredicates() +
                ", limit=" + node.getLimit() +
                "}";
    }

    @Override
    public String visitLogicalMysqlScan(LogicalMysqlScanOperator node, Void context) {
        return "LogicalMysqlScanOperator" + " {" + "table=" + node.getTable().getCatalogTableName() +
                ", outputColumns=" + new ArrayList<>(node.getColRefToColumnMetaMap().keySet()) +
                ", predicate=" + node.getPredicate() +
                ", limit=" + node.getLimit() +
                "}";
    }

    @Override
    public String visitLogicalMetaScan(LogicalMetaScanOperator node, Void context) {
        return super.visitLogicalMetaScan(node, context);
    }

    @Override
    public String visitLogicalEsScan(LogicalEsScanOperator node, Void context) {
        return "LogicalEsScanOperator" + " {" + "selectedIndex=" + node.getSelectedIndex() +
                ", outputColumns=" + new ArrayList<>(node.getColRefToColumnMetaMap().keySet()) +
                ", predicate=" + node.getPredicate() +
                ", limit=" + node.getLimit() +
                "}";
    }

    @Override
    public String visitLogicalJDBCScan(LogicalJDBCScanOperator node, Void context) {
        return "LogicalJDBCScanOperator" + " {" + "table=" + node.getTable().getCatalogTableName() +
                ", outputColumns=" + new ArrayList<>(node.getColRefToColumnMetaMap().keySet()) +
                ", predicate=" + node.getPredicate() +
                ", limit=" + node.getLimit() +
                "}";
    }

    @Override
    public String visitLogicalProject(LogicalProjectOperator node, Void context) {
        StringBuilder sb = new StringBuilder("LogicalProjectOperator {projection=");
        sb.append(new ArrayList<>(node.getColumnRefMap().values()));
        sb.append("}");
        return sb.toString();
    }

    @Override
    public String visitLogicalJoin(LogicalJoinOperator node, Void context) {
        return super.visitLogicalJoin(node, context);
    }

    @Override
    public String visitLogicalAggregation(LogicalAggregationOperator node, Void context) {
        return "LogicalAggregation" + " {type=" + node.getType() +
                " ,aggregations=" + node.getAggregations() +
                " ,groupKeys=" + node.getGroupingKeys() +
                " ,projection=" + node.getProjection() +
                " ,predicate=" + node.getPredicate() +
                "}";
    }

    @Override
    public String visitLogicalTopN(LogicalTopNOperator node, Void context) {
        return "LogicalTopNOperator" + " {phase=" + node.getSortPhase().toString() +
                ", orderBy=" + node.getOrderByElements() +
                ", limit=" + node.getLimit() +
                ", offset=" + node.getOffset() +
                "}";
    }

    @Override
    public String visitLogicalAssertOneRow(LogicalAssertOneRowOperator node, Void context) {
        return super.visitLogicalAssertOneRow(node, context);
    }

    @Override
    public String visitLogicalAnalytic(LogicalWindowOperator node, Void context) {
        StringBuilder sb = new StringBuilder("LogicalWindowOperator");
        sb.append(" {window=").append(node.getWindowCall());
        sb.append(", partitions=").append(node.getPartitionExpressions());
        sb.append(", orderBy=").append(node.getOrderByElements());
        sb.append(", enforceSort").append(node.getEnforceSortColumns());
        sb.append("}");
        return sb.toString();
    }

    @Override
    public String visitLogicalUnion(LogicalUnionOperator node, Void context) {
        return getSetOperationBuilder("LogicalUnionOperator", node.getOutputColumnRefOp(),
                node.getChildOutputColumns());
    }

    @Override
    public String visitLogicalExcept(LogicalExceptOperator node, Void context) {
        return getSetOperationBuilder("LogicalExceptOperator", node.getOutputColumnRefOp(),
                node.getChildOutputColumns());
    }

    @Override
    public String visitLogicalIntersect(LogicalIntersectOperator node, Void context) {
        return getSetOperationBuilder("LogicalIntersectOperator", node.getOutputColumnRefOp(),
                node.getChildOutputColumns());
    }

    @Override
    public String visitLogicalValues(LogicalValuesOperator node, Void context) {
        return super.visitLogicalValues(node, context);
    }

    @Override
    public String visitLogicalRepeat(LogicalRepeatOperator node, Void context) {
        return super.visitLogicalRepeat(node, context);
    }

    @Override
    public String visitLogicalFilter(LogicalFilterOperator node, Void context) {
        return "LogicalFilterOperator" + " {" + "predicate=" + node.getPredicate() + "}";
    }

    @Override
    public String visitLogicalTableFunction(LogicalTableFunctionOperator node, Void context) {
        return super.visitLogicalTableFunction(node, context);
    }

    @Override
    public String visitLogicalLimit(LogicalLimitOperator node, Void context) {
        return "LogicalLimitOperator {" + node.getPhase().name() + " limit=" + node.getLimit() +
                ", offset=" + node.getOffset() +
                "}";
    }

    @Override
    public String visitLogicalCTEAnchor(LogicalCTEAnchorOperator node, Void context) {
        return super.visitLogicalCTEAnchor(node, context);
    }

    @Override
    public String visitLogicalCTEConsume(LogicalCTEConsumeOperator node, Void context) {
        return super.visitLogicalCTEConsume(node, context);
    }

    @Override
    public String visitLogicalCTEProduce(LogicalCTEProduceOperator node, Void context) {
        return super.visitLogicalCTEProduce(node, context);
    }

    @Override
    public String visitMockOperator(MockOperator node, Void context) {
        return super.visitMockOperator(node, context);
    }

    @Override
    public String visitPhysicalDistribution(PhysicalDistributionOperator node, Void context) {
        return "PhysicalDistributionOperator" + " {distributionSpec=" + node.getDistributionSpec() +
                " ,globalDict=" + node.getGlobalDicts() +
                "}";
    }

    @Override
    public String visitPhysicalProject(PhysicalProjectOperator node, Void context) {
        StringBuilder sb = new StringBuilder("PhysicalProjectOperator {projection=");
        sb.append(new ArrayList<>(node.getColumnRefMap().values()));
        sb.append("}");
        return sb.toString();
    }

    @Override
    public String visitPhysicalHashAggregate(PhysicalHashAggregateOperator node, Void context) {
        return "PhysicalHashAggregate" + " {type=" + node.getType() +
                ", groupBy=" + node.getGroupBys() +
                ", partitionBy=" + node.getPartitionByColumns() +
                " ,aggregations=" + node.getAggregations() +
                "}";
    }

    @Override
    public String visitPhysicalHashJoin(PhysicalHashJoinOperator node, Void context) {
        return super.visitPhysicalHashJoin(node, context);
    }

    @Override
    public String visitPhysicalNestLoopJoin(PhysicalNestLoopJoinOperator node, Void context) {
        return node.toString();
    }

    @Override
    public String visitPhysicalOlapScan(PhysicalOlapScanOperator node, Void context) {
        return "PhysicalOlapScanOperator" + " {" + "table=" + node.getTable().getId() +
                ", selectedPartitionId=" + node.getSelectedPartitionId() +
                ", selectedIndexId=" + node.getSelectedIndexId() +
                ", outputColumns=" + node.getOutputColumns() +
                ", projection=" + node.getProjection() +
                ", predicate=" + node.getPredicate() +
                ", prunedPartitionPredicates=" + node.getPrunedPartitionPredicates() +
                ", limit=" + node.getLimit() +
                "}";
    }

    @Override
    public String visitPhysicalHiveScan(PhysicalHiveScanOperator node, Void context) {
        return "PhysicalHiveScanOperator" + " {" + "table=" + node.getTable().getCatalogTableName() +
                ", outputColumns=" + new ArrayList<>(node.getColRefToColumnMetaMap().keySet()) +
                ", predicates=" + node.getScanOperatorPredicates() +
                ", limit=" + node.getLimit() +
                "}";
    }

    @Override
    public String visitPhysicalIcebergScan(PhysicalIcebergScanOperator node, Void context) {
        StringBuilder sb = new StringBuilder("PhysicalIcebergScanOperator");
        sb.append(" {").append("table=").append(node.getTable().getCatalogTableName())
                .append(", outputColumns=").append(new ArrayList<>(node.getColRefToColumnMetaMap().keySet()))
                .append(", predicates=").append(node.getScanOperatorPredicates())
                .append("}");
        return sb.toString();
    }

    @Override
    public String visitPhysicalHudiScan(PhysicalHudiScanOperator node, Void context) {
        return "PhysicalHudiScanOperator" + " {" + "table=" + node.getTable().getCatalogTableName() +
                ", outputColumns=" + new ArrayList<>(node.getColRefToColumnMetaMap().keySet()) +
                ", predicates=" + node.getScanOperatorPredicates() +
                ", limit=" + node.getLimit() +
                "}";
    }

    @Override
    public String visitPhysicalSchemaScan(PhysicalSchemaScanOperator node, Void context) {
        return super.visitPhysicalSchemaScan(node, context);
    }

    @Override
    public String visitPhysicalMysqlScan(PhysicalMysqlScanOperator node, Void context) {
        return "PhysicalMysqlScanOperator" + " {" + "table=" + node.getTable().getCatalogTableName() +
                ", outputColumns=" + new ArrayList<>(node.getColRefToColumnMetaMap().keySet()) +
                ", predicate=" + node.getPredicate() +
                ", limit=" + node.getLimit() +
                "}";
    }

    @Override
    public String visitPhysicalEsScan(PhysicalEsScanOperator node, Void context) {
        return "PhysicalEsScanOperator" + " {" + "selectedIndex=" + node.getSelectedIndex() +
                ", outputColumns=" + new ArrayList<>(node.getColRefToColumnMetaMap().keySet()) +
                ", predicate=" + node.getPredicate() +
                ", limit=" + node.getLimit() +
                "}";
    }

    @Override
    public String visitPhysicalMetaScan(PhysicalMetaScanOperator node, Void context) {
        return super.visitPhysicalMetaScan(node, context);
    }

    @Override
    public String visitPhysicalJDBCScan(PhysicalJDBCScanOperator node, Void context) {
        return "PhysicalJDBCScanOperator" + " {" + "table=" + node.getTable().getCatalogTableName() +
                ", outputColumns=" + new ArrayList<>(node.getColRefToColumnMetaMap().keySet()) +
                ", predicate=" + node.getPredicate() +
                ", limit=" + node.getLimit() +
                "}";
    }

    @Override
    public String visitPhysicalTopN(PhysicalTopNOperator node, Void context) {
        return "PhysicalTopNOperator" + " {phase=" + node.getSortPhase() +
                ", orderBy=" + node.getOrderSpec() +
                ", limit=" + node.getLimit() +
                ", offset=" + node.getOffset() +
                "}";
    }

    @Override
    public String visitPhysicalAssertOneRow(PhysicalAssertOneRowOperator node, Void context) {
        return super.visitPhysicalAssertOneRow(node, context);
    }

    @Override
    public String visitPhysicalAnalytic(PhysicalWindowOperator node, Void context) {
        return super.visitPhysicalAnalytic(node, context);
    }

    @Override
    public String visitPhysicalUnion(PhysicalUnionOperator node, Void context) {
        return getSetOperationBuilder("PhysicalUnionOperator", node.getOutputColumnRefOp(),
                node.getChildOutputColumns());
    }

    @Override
    public String visitPhysicalExcept(PhysicalExceptOperator node, Void context) {
        return getSetOperationBuilder("PhysicalExceptOperator", node.getOutputColumnRefOp(),
                node.getChildOutputColumns());
    }

    @Override
    public String visitPhysicalIntersect(PhysicalIntersectOperator node, Void context) {
        return getSetOperationBuilder("PhysicalIntersectOperator", node.getOutputColumnRefOp(),
                node.getChildOutputColumns());
    }

    @NotNull
    private String getSetOperationBuilder(String name, List<ColumnRefOperator> outputColumnRefOp,
                                          List<List<ColumnRefOperator>> childOutputColumns) {
        StringBuilder sb = new StringBuilder(name);
        sb.append("{");
        sb.append("output=[").append(outputColumnRefOp.stream().map(ColumnRefOperator::toString)
                .collect(Collectors.joining(", "))).append("], ");

        String child = childOutputColumns.stream()
                .map(l -> l.stream().map(ColumnRefOperator::toString).collect(Collectors.joining(", ")))
                .collect(Collectors.joining(", "));

        sb.append(child).append("}");
        return sb.toString();
    }

    @Override
    public String visitPhysicalValues(PhysicalValuesOperator node, Void context) {
        return super.visitPhysicalValues(node, context);
    }

    @Override
    public String visitPhysicalRepeat(PhysicalRepeatOperator node, Void context) {
        return super.visitPhysicalRepeat(node, context);
    }

    @Override
    public String visitPhysicalFilter(PhysicalFilterOperator node, Void context) {
        return super.visitPhysicalFilter(node, context);
    }

    @Override
    public String visitPhysicalTableFunction(PhysicalTableFunctionOperator node, Void context) {
        return super.visitPhysicalTableFunction(node, context);
    }

    @Override
    public String visitPhysicalLimit(PhysicalLimitOperator node, Void context) {
        return "PhysicalLimitOperator" + " {limit=" + node.getLimit() +
                ", offset=" + node.getOffset() +
                "}";
    }

    @Override
    public String visitPhysicalCTEAnchor(PhysicalCTEAnchorOperator node, Void context) {
        return super.visitPhysicalCTEAnchor(node, context);
    }

    @Override
    public String visitPhysicalCTEProduce(PhysicalCTEProduceOperator node, Void context) {
        return super.visitPhysicalCTEProduce(node, context);
    }

    @Override
    public String visitPhysicalCTEConsume(PhysicalCTEConsumeOperator node, Void context) {
        return super.visitPhysicalCTEConsume(node, context);
    }

    @Override
    public String visitPhysicalNoCTE(PhysicalNoCTEOperator node, Void context) {
        return super.visitPhysicalNoCTE(node, context);
    }
}
