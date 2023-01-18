//
// Created by dewe on 12/31/22.
//
#include "core.h"
#include <arrow/compute/cast.h>
#include <boost/chrono/duration.hpp>
#include <future>
#include "arrow/compute/exec.h"
#include "dataframe.h"
#include "group_by.h"


typedef boost::date_time::subsecond_duration<time_duration,1000000000> nanosec;
typedef boost::date_time::subsecond_duration<time_duration,1000000000> nanoseconds;

namespace pd {

std::pair<std::string, int> splitTimeSpan(std::string const& freq)
{
    auto it = std::find_if(
        freq.begin(),
        freq.end(),
        [](int x) { return std::isalpha(x); });

    std::ostringstream ss;
    std::string freq_unit, freqValueStr;
    int freq_value = 1;

    copy(it, freq.end(), std::ostream_iterator<uint8_t>(ss));
    freq_unit = ss.str();

    if (freq.begin() != it)
    {
        std::ostringstream ss2;
        copy(freq.begin(), it, std::ostream_iterator<uint8_t>(ss2));
        freqValueStr = ss2.str();
        freq_value = std::stoi(freqValueStr);
    }
    return { freq_unit, freq_value };
}

template<class Iterator = day_iterator, typename FreqTime = int>
std::shared_ptr<arrow::Array> date_range(
    date const& start,
    date const& end,
    FreqTime freq = 1,
    std::string const& tz = "")
{
    Iterator it(start, freq);
    auto builderResult = arrow::MakeBuilder(
        std::make_shared<arrow::TimestampType>(arrow::TimeUnit::NANO, tz));

    if (builderResult.ok())
    {
        auto builder = builderResult.MoveValueUnsafe();

        while (*it <= end)
        {
            arrow::TimestampScalar ts(fromDate(*it), arrow::TimeUnit::NANO, tz);
            auto status = builder->AppendScalar(ts);
            if (status.ok())
            {
                ++it;
            }
            else
            {
                throw std::runtime_error(status.ToString());
            }
        }
        return builder->Finish().MoveValueUnsafe();
    }
    throw std::runtime_error(builderResult.status().ToString());
}

template<class Iterator = day_iterator, typename FreqTime = int>
std::shared_ptr<arrow::Array> date_range(
    date const& start,
    int period,
    FreqTime freq,
    std::string const& tz = "")
{
    Iterator it(start, freq);
    auto builderResult = arrow::MakeBuilder(
        std::make_shared<arrow::TimestampType>(arrow::TimeUnit::NANO, tz));

    if (builderResult.ok())
    {
        auto builder = builderResult.MoveValueUnsafe();

        while (period > 0)
        {
            arrow::TimestampScalar ts(fromDate(*it), arrow::TimeUnit::NANO, tz);
            auto status = builder->AppendScalar(ts);
            if (status.ok())
            {
                ++it;
                period--;
            }
            else
            {
                throw std::runtime_error(status.ToString());
            }
        }
        return builder->Finish().MoveValueUnsafe();
    }
    throw std::runtime_error(builderResult.status().ToString());
}

std::shared_ptr<arrow::Array> switchFunction(
    date const& start,
    auto const& end_or_period,
    std::string const& freq,
    std::string const& tz)
{
    auto [freq_unit, freq_value] = splitTimeSpan(freq);
    if (freq_unit == "D")
    {
        return date_range<day_iterator>(start, end_or_period, freq_value, tz);
    }
    else if (freq_unit == "W")
    {
        return date_range<week_iterator>(start, end_or_period, freq_value, tz);
    }
    else if (freq_unit == "SM")
    {
        return date_range<month_iterator>(
            firstDateOfMonth(start),
            end_or_period,
            freq_value,
            tz);
    }
    else if (freq_unit == "M")
    {
        return date_range<month_iterator>(
            start.end_of_month(),
            end_or_period,
            freq_value,
            tz);
    }
    else if (freq_unit == "Y")
    {
        return date_range<year_iterator>(
            lastDateOfYear(start),
            end_or_period,
            freq_value,
            tz);
    }
    else if (freq_unit == "YS")
    {
        return date_range<year_iterator>(
            firstDateOfYear(start),
            end_or_period,
            freq_value,
            tz);
    }
    throw std::runtime_error(
        "date_range with start:date_type is only compatible with [D W M Y] freq_unit");
}

std::shared_ptr<arrow::Array> switchFunction(
    ptime const& start,
    auto const& end_or_period,
    std::string const& freq,
    std::string const& tz)
{
    auto [freq_unit, freq_value] = splitTimeSpan(freq);
    if (freq_unit == "T" or freq_unit == "min")
    {
        return date_range(start, end_or_period, minutes(freq_value), tz);
    }
    else if (freq_unit == "S")
    {
        return date_range(start, end_or_period, seconds(freq_value), tz);
    }
    else if (freq_unit == "L" or freq_unit == "ms")
    {
        return date_range(start, end_or_period, milliseconds(freq_value), tz);
    }
    else if (freq_unit == "U" or freq_unit == "us")
    {
        return date_range(start, end_or_period, microseconds(freq_value), tz);
    }
    else if (freq_unit == "N" or freq_unit == "ns")
    {
        return date_range(start, end_or_period, nanoseconds(freq_value), tz);
    }
    throw std::runtime_error(
        "date_range with start:ptime_type is only compatible with "
        "[T/min S L/ms U/us N/ns] freq_unit");
}

std::shared_ptr<arrow::Array> date_range(
    ptime const& start,
    ptime const& end,
    time_duration const& freq,
    std::string const& tz)
{
    time_iterator it(start, freq);
    auto builderResult = arrow::MakeBuilder(
        std::make_shared<arrow::TimestampType>(arrow::TimeUnit::NANO, tz));

    if (builderResult.ok())
    {
        auto builder = builderResult.MoveValueUnsafe();

        while (*it <= end)
        {
            arrow::TimestampScalar ts(
                fromPTime(*it),
                arrow::TimeUnit::NANO,
                tz);
            auto status = builder->AppendScalar(ts);
            if (status.ok())
            {
                ++it;
            }
            else
            {
                throw std::runtime_error(status.ToString());
            }
        }
        return builder->Finish().MoveValueUnsafe();
    }
    throw std::runtime_error(builderResult.status().ToString());
}

std::shared_ptr<arrow::Array> date_range(
    ptime const& start,
    int period,
    time_duration const& freq,
    std::string const& tz)
{
    time_iterator it(start, freq);
    auto builderResult = arrow::MakeBuilder(
        std::make_shared<arrow::TimestampType>(arrow::TimeUnit::NANO, tz));

    if (builderResult.ok())
    {
        auto builder = builderResult.MoveValueUnsafe();

        while (period > 0)
        {
            arrow::TimestampScalar ts(
                fromPTime(*it),
                arrow::TimeUnit::NANO,
                tz);
            auto status = builder->AppendScalar(ts);
            if (status.ok())
            {
                ++it;
                period--;
            }
            else
            {
                throw std::runtime_error(status.ToString());
            }
        }
        return builder->Finish().MoveValueUnsafe();
    }
    throw std::runtime_error(builderResult.status().ToString());
}

std::shared_ptr<arrow::Array> date_range(
    date const& start,
    date const& end,
    std::string const& freq,
    std::string const& tz)
{

    return switchFunction(start, end, freq, tz);
}

std::shared_ptr<arrow::Array> date_range(
    date const& start,
    int period,
    std::string const& freq,
    std::string const& tz)
{
    return switchFunction(start, period, freq, tz);
}

std::shared_ptr<arrow::Array> date_range(
    ptime const& start,
    ptime const& end,
    std::string const& freq,
    std::string const& tz)
{
    return switchFunction(start, end, freq, tz);
}

std::shared_ptr<arrow::Array> date_range(
    ptime const& start,
    int period,
    std::string const& freq,
    std::string const& tz)
{
    return switchFunction(start, period, freq, tz);
}

std::shared_ptr<arrow::Array> range(int64_t start, int64_t end)
{
    arrow::Int64Builder builder;
    auto rangeView = std::views::iota(start, end);
    auto status = builder.AppendValues(rangeView.begin(), rangeView.end());
    if (status.ok())
    {
        return builder.Finish().MoveValueUnsafe();
    }
    else
    {
        throw std::runtime_error(status.ToString());
    }
}


auto generate_bins_dt64(std::shared_ptr<arrow::Int64Array> const& values,
                        std::shared_ptr<arrow::Int64Array> const& binner,
                        bool closed_right)
{
    if(pd::Series(values, false).count_na() > 0)
    {
        throw std::runtime_error("value in generate_bins_dt64 contains nan");
    }

    int64_t lenidx = values->length();
    int64_t lenbin = binner->length();

    if(lenidx <= 0 or lenbin <= 0)
    {
        throw std::runtime_error("Invalid length for values or for binner");
    }

    if(values->GetView(0) < binner->GetView(0))
    {
        throw std::runtime_error("Values falls before first bin");
    }

    if(values->GetView(lenidx - 1) > binner->GetView(lenbin - 1) )
    {
        throw std::runtime_error("Values falls after last bin")   ;
    }

    auto bins = std::vector<int64_t>(lenbin-1);

    int64_t j = 0, bc = 0;
    int64_t r_bin{};
    if(closed_right)
    {
        for(int64_t i = 0; i < lenbin-1; i++)
        {
            r_bin = binner->GetView(i + 1);
            while(j < lenidx and values->GetView(j) <= r_bin)
            {
                j++;
            }
            bins[bc] = j;
            bc++;
        }
    }
    else
    {
        for(int64_t i = 0; i < lenbin-1; i++)
        {
            r_bin = binner->GetView(i + 1);
            while(j < lenidx and  values->GetView(j) < r_bin)
            {
                j++;
            }
            bins[bc] = j;
            bc++;
        }
    }

    return bins;
}

class GroupBy resample(
    pd::DataFrame const& df,
    time_duration const& freq,
    bool closed_right,
    bool label_right,
    std::string const& tz)
{
    auto ax = df.index();
    if (ax.size() > 2)
    {
        int64_t N = ax.size();

        auto first = time_from_string(ax[0].scalar->ToString());
        auto last = time_from_string(ax[N - 1].scalar->ToString());

        auto binner = date_range(first, last, freq, tz);
        auto ax_values = ax.cast<int64_t>();

        auto bin_edges = Series(binner, false).cast<int64_t>();

        auto bins = generate_bins_dt64(
            std::dynamic_pointer_cast<arrow::Int64Array>(ax_values.array()),
            std::dynamic_pointer_cast<arrow::Int64Array>(bin_edges.array()),
            closed_right);

        if (closed_right)
        {
            if (label_right)
            {
                bin_edges = bin_edges[Slice{ 1 }];
            }
        }
        else if (label_right)
        {
            bin_edges = bin_edges[Slice{ 1 }];
        }

        if (bins.size() < bin_edges.size())
        {
            bin_edges = bin_edges[Slice{ 0, int(bins.size()) }];
        }

        arrow::Int64Builder builder;
        auto status = builder.Reserve(N);

        if (status.ok())
        {
            int64_t l = 0;
            int i = 0;
            for (int64_t r : bins)
            {
                auto bin_edge = *bin_edges[i++].scalar;
                status = builder.AppendScalar(bin_edge, r - l);

                if (status.ok())
                {
                    l = r;
                }
                else
                {
                    throw std::runtime_error(status.ToString());
                }
            }
            auto arr = builder.Finish().MoveValueUnsafe();

            arrow::compute::CastOptions opt{ true };
            opt.to_type = TimestampTypePtr;
            arr = ValidateHelper(arrow::compute::Cast(arr, opt)).m_array;

            return pd::GroupBy{ arr, df.array() };
        }
        else
        {
            throw std::runtime_error(status.ToString());
        }
    }
    else
    {
        throw std::runtime_error("Cannot Resample a df of rows < 2");
    }
}

    std::shared_ptr<arrow::Array>
    combineIndexes(std::vector<Series::ArrayType> const& indexes,
                   bool ignore_index)
    {
        if(ignore_index)
        {
            std::vector<int64_t> idx_len(indexes.size());
            std::ranges::transform(indexes,
                                   idx_len.begin(),
                                   [](Series::ArrayType const& idx){
                                       return idx->length();
                                   });
            return range(
                0L,
                std::accumulate(idx_len.begin(), idx_len.end(), 0L));
        }

        auto result = arrow::Concatenate(indexes);
        if(result.ok())
            return result.MoveValueUnsafe();

        throw std::runtime_error(result.status().ToString());
    }

pd::DataFrame concatRows(std::vector<pd::DataFrame> const& objs,
                         JoinType join,
                         bool ignore_index,
                         bool sort)
{
    std::vector<
        std::pair<std::shared_ptr<arrow::Field>,
            std::vector<Series::ArrayType> > > newDf;
    std::vector<Series::ArrayType> newIdx;

    auto insertNewDf = [&newDf](std::shared_ptr<arrow::Field> const& col,
                                pd::DataFrame const& df,
                                Series::ArrayType const& arr)
    {
        auto col_name = col->name();
        if(auto it =
                std::ranges::find_if(newDf,
                                     [&col_name](auto const& item)
                                     {
                                         return item.first->name() == col_name;
                                     }); it != newDf.end() )
        {
            it->second.emplace_back(arr);
        }
        else
        {
            newDf.emplace_back(col, std::vector<Series::ArrayType>{arr});
        }
    };

    auto newColumns = mergeColumns(objs, join);

    for(pd::DataFrame const& df: objs)
    {
        auto schema  = df.array()->schema();

        for(std::shared_ptr<arrow::Field> const& col: newColumns)
        {
            auto col_name = col->name();
            int index = schema->GetFieldIndex(col_name);
            if(index != -1)
            {
                insertNewDf(col, df,
                            df.array()->column(index));
            }
            else
            {
                auto result = arrow::MakeArrayOfNull(col->type(),
                                                        df.num_rows());
                if(result.ok())
                {
                    auto nullArray = result.MoveValueUnsafe();
                    insertNewDf(col, df, nullArray);
                }
                else
                {
                    throw std::runtime_error(result.status().ToString());
                }
            }
        }

        newIdx.emplace_back(df.indexArray());
    }

    if(sort)
    {
        std::ranges::sort(newDf,
                          [](auto const& a, auto const& b){

                              return a.first->name() < b.first->name();
                          });
    }

    arrow::FieldVector fieldVector;
    arrow::ArrayVector arrayVectors;

    for(auto const& [field, arr]: newDf)
    {
        fieldVector.push_back(field);
        auto result = arrow::Concatenate(arr);
        if(result.ok())
        {
            arrayVectors.push_back(result.MoveValueUnsafe());
        }
        else
        {
            throw std::runtime_error(result.status().ToString());
        }
    }

    int64_t numRows = arrayVectors[0]->length();
    auto rb =  arrow::RecordBatch::Make(arrow::schema(fieldVector),
                                       numRows,
                                       arrayVectors);

    return {rb, combineIndexes(newIdx, ignore_index)};
}

Series ValidateHelper(auto && result)
{
    if (result.ok()) {
        return pd::Series{result->make_array(), false, ""};
    }
    throw std::runtime_error(result.status().ToString());
}

pd::DataFrame concatColumnsUnsafe(std::vector<pd::DataFrame> const& objs)
{
    auto df = objs.at(0).array();
    auto N = df->num_columns();

    for(int i = 1 ; i < objs.size(); i++)
    {
        for (auto const& field : objs[i].array()->schema()->fields())
        {
            auto result = df->AddColumn(N++, field,
                                        objs[i][field->name()].m_array);
            if(result.ok())
            {
                df = result.MoveValueUnsafe();
            }else
            {
                throw std::runtime_error(result.status().ToString());
            }
        }
    }
    return {df,
            objs.at(0).indexArray()};
}

pd::DataFrame concatColumns(std::vector<pd::DataFrame> const& objs,
                            JoinType join,
                            bool ignore_index,
                            bool sort)
{
    std::vector<
        std::pair<std::shared_ptr<arrow::Field>, Series::ArrayType> > newDf;

    auto newIdx = mergeRows(objs, join);

    if(sort)
    {
        auto idx = arrow::compute::SortIndices(*newIdx).MoveValueUnsafe();
        newIdx = ValidateHelper(
            arrow::compute::CallFunction("array_take", {newIdx, idx})).m_array;
    }

    std::vector<std::future<std::pair<std::shared_ptr<arrow::Field>, Series::ArrayType>>> futures;
    for(pd::DataFrame const& df: objs)
    {
        auto schema = df.array()->schema();
        auto df_index = df.index();

        for (std::shared_ptr<arrow::Field> const& col : schema->fields())
        {
            futures.emplace_back(std::async(
                [c = col, d = df, n = newIdx, j = df_index]()
                {
                    auto col_name = c->name();
                    auto arr = d.m_array->GetColumnByName(col_name);

                    if (not arr->Equals(n))
                    {
                        auto builder =
                            arrow::MakeBuilder(c->type()).MoveValueUnsafe();

                        auto status = builder->Reserve(n->length());
                        if(builder->Reserve(n->length()).ok())
                        {
                            for (int i = 0; i < n->length(); i++)
                            {
                                auto idx = n->GetScalar(i).MoveValueUnsafe();
                                auto result = j.index(idx);
                                status =  (result == -1 ?
                                                   builder->AppendNull() :
                                                   builder->AppendScalar(
                                                       *arr->GetScalar(result)
                                                            .MoveValueUnsafe()) );
                                if(status.ok() )
                                {
                                    continue;
                                }
                                else
                                {
                                    throw std::runtime_error(status.ToString());
                                }
                            }
                        }else
                        {
                            throw std::runtime_error(status.ToString());
                        }
                        arr = builder->Finish().MoveValueUnsafe();
                    }
                    return std::pair{ c, arr };
                }));
        }
    }

    arrow::FieldVector fieldVector;
    arrow::ArrayVector arrayVectors;

    int i = 0;
    for(auto& ftr: futures)
    {
        auto [field, arr] = ftr.get();
        if(ignore_index)
        {
            field = arrow::field(std::to_string(i++), field->type());
        }
        fieldVector.push_back(field);
        arrayVectors.push_back(arr);
    }

    int64_t numRows = arrayVectors[0]->length();
    auto rb =  arrow::RecordBatch::Make(arrow::schema(fieldVector),
                                       numRows,
                                       arrayVectors);

    return {rb, newIdx};
}

pd::DataFrame concat(std::vector<pd::DataFrame> const& objs,
                     AxisType axis_type,
                     JoinType join,
                     bool ignore_index,
                     bool sort)
{
    auto fn = axis_type == AxisType::Index ? concatRows : concatColumns;
    return fn(objs, join, ignore_index, sort);
}


std::vector<std::shared_ptr<arrow::Field> >
mergeColumns(std::vector<pd::DataFrame> const& objs,
             JoinType join)
{
    std::unordered_map<std::shared_ptr<arrow::Field>, int64_t> order;
    std::vector<std::shared_ptr<arrow::Field>> result =
        objs[0].array()->schema()->fields();

    int i = 0;
    for(auto const& field: result)
    {
        order[field] = i++;
    }

    auto fieldEqual =  [](auto const& l, auto const& r){
        return l->name() < r->name();
    };

    std::for_each(
        objs.begin() + 1,
        objs.end(),
        [join, &result, &fieldEqual, &order, &i](auto const& df)
        {
            auto fields = df.array()->schema()->fields();
            std::vector<std::shared_ptr<arrow::Field>> temp;
            if (join == JoinType::Inner)
            {
                std::set_intersection(
                    result.begin(),
                    result.end(),
                    fields.begin(),
                    fields.end(),
                    std::back_inserter(temp),
                    fieldEqual);
            }
            else
            {
                std::set_union(
                    result.begin(),
                    result.end(),
                    fields.begin(),
                    fields.end(),
                    std::back_inserter(temp),
                    fieldEqual);
            }

            for(auto const& field: fields)
            {
                if(not order.contains(field))
                {
                    order[field] = i++;
                }
            }
            result = temp;
        });

    std::ranges::sort(result, [&order](auto const& l, auto const& r){
                          return order[l] < order[r];
    });
    return result;
}

std::shared_ptr<arrow::Array>
mergeRows(std::vector<pd::DataFrame> const& objs,
             JoinType join)
{
    std::vector<std::shared_ptr<arrow::Array>> indexes(objs.size());
    std::ranges::transform(
        objs,
        indexes.begin(),
        [](auto const& obj) { return obj.indexArray(); });

    if (join == JoinType::Inner)
    {
        auto index = indexes[0];
        for (auto other = indexes.begin() + 1; other != indexes.end(); other++)
        {
            index = Series(index, true).intersection(Series{*other, true}).m_array;
        }
        return index;
    }
    else
    {
        return ValidateAndReturn(arrow::compute::Unique(
            ValidateAndReturn(arrow::Concatenate(indexes))));
    }
}

}