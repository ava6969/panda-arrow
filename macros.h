#pragma once
#include "tbb/parallel_for.h"


struct BinaryOperatorFunctor {
    std::vector<std::shared_ptr<arrow::Array>> const& lhs;
    std::vector<std::shared_ptr<arrow::Array>> const& rhs;
    std::vector<std::shared_ptr<arrow::ArrayData>>& result;
    const char* func;

    BinaryOperatorFunctor(
        std::vector<std::shared_ptr<arrow::Array>> const& lhs,
        std::vector<std::shared_ptr<arrow::Array>> const& rhs,
        std::vector<std::shared_ptr<arrow::ArrayData>>& result,
        const char* function)
        : lhs(lhs), rhs(rhs), func(function), result(result) {}

    void operator()(const tbb::blocked_range<size_t>& range) const {
        for (size_t i = range.begin(); i < range.end(); ++i) {
            auto res = arrow::compute::CallFunction(func, { lhs[i], rhs[i] });
            if (res.ok()) {
                result[i] = res.MoveValueUnsafe().array();
            } else {
                throw std::runtime_error(res.status().ToString());
            }
        }
    }
};

#define BINARY_OPERATOR_PARALLEL_DF(sign, func)                                                        \
    DataFrame DataFrame::operator sign(DataFrame const &other) const {                                  \
        std::vector<std::shared_ptr<arrow::ArrayData>> df_result(m_array->num_columns());               \
        BinaryOperatorFunctor functor(m_array->columns(),                                             \
                                      other.m_array->columns(),                                       \
                                      df_result, #func); \
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_array->num_columns()), functor);               \
        return {m_array->schema(), m_array->num_rows(), df_result};                                     \
    }

#define BINARY_OPERATOR_PARALLEL_SCALAR_OR_SERIES(T, sign, func)                                                                                                      \
    DataFrame DataFrame::operator sign(T const &other) const { \
        std::vector<std::shared_ptr<arrow::ArrayData>> df_result; \
        df_result.reserve(m_array->num_columns()); \
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_array->num_columns()), [&](const tbb::blocked_range<size_t> &r) { \
            for (size_t i = r.begin(); i < r.end(); ++i) { \
                auto col = m_array->column(i); \
                auto result = arrow::compute::CallFunction(#func, {col, other.value()}); \
                if (result.ok()) { \
                    df_result[i] = result.MoveValueUnsafe().array(); \
                } else { \
                    throw std::runtime_error(result.status().ToString()); \
                } \
            } \
        }); \
        return {m_array->schema(), m_array->num_rows(), df_result}; \
    }                                                                                               \
    DataFrame operator sign(T const &other, DataFrame const &df) { \
        return df.operator sign(other); \
    }
#define BINARY_OPERATOR_PARALLEL(sign, func) \
BINARY_OPERATOR_PARALLEL_DF(sign, func) \
BINARY_OPERATOR_PARALLEL_SCALAR_OR_SERIES(Series, sign, func) \
BINARY_OPERATOR_PARALLEL_SCALAR_OR_SERIES(Scalar, sign, func)

#define GROUPBY_NUMERIC_AGG(func, T) \
arrow::Result<pd::DataFrame> GroupBy:: func(std::vector<std::string> const& args) \
{ \
    auto schema = df.m_array->schema(); \
    auto N = groups.size(); \
\
auto fv = fieldVectors(args, schema); \
    arrow::ArrayDataVector arr(args.size()); \
\
tbb::parallel_for( \
        tbb::blocked_range<size_t>(0, args.size()), \
        [&](const tbb::blocked_range<size_t>& r) \
        { \
            for (size_t i = r.begin(); i != r.end(); ++i) \
            { \
                const auto& arg = args[i]; \
                long L = unique_value->length(); \
                int index = schema->GetFieldIndex(arg); \
\
                std::vector<T> result(L); \
                tbb::parallel_for( \
                    tbb::blocked_range<size_t>(0, unique_value->length()), \
                    [&](const tbb::blocked_range<size_t>& r) \
                    { \
                        for (size_t j = r.begin(); j != r.end(); ++j) \
                        { \
                            auto key = unique_value->GetScalar(long(j)) \
               .MoveValueUnsafe(); \
                            auto& group = groups.at(key); \
\
arrow::Datum d = arrow::compute::CallFunction( \
                                                 #func,\
                                                 { group[index] }, \
                                                 nullptr) \
                                                 .MoveValueUnsafe(); \
                            result[j] = \
    d.scalar_as<arrow::CTypeTraits<T>::ScalarType>().value; \
                        } \
                    }); \
\
                auto builder = arrow::CTypeTraits<T>::BuilderType(); \
                    builder.AppendValues(result); \
\
                std::shared_ptr<arrow::ArrayData> data; \
                    builder.FinishInternal(&data); \
                arr[i] = data; \
            } \
        }); \
\
    fv.emplace_back(arrow::field(keyStr, unique_value->type())); \
        arr.push_back(unique_value->data()); \
\
    return pd::DataFrame(arrow::schema(fv), long(N), arr); \
} \
\
arrow::Result<pd::Series> GroupBy:: func(std::string const& arg) \
{ \
    auto schema = df.m_array->schema(); \
    auto N = groups.size(); \
\
auto fv = schema->GetFieldByName(arg); \
\
    long L = unique_value->length(); \
    int index = schema->GetFieldIndex(arg); \
\
std::vector<T> result(L); \
    tbb::parallel_for( \
        tbb::blocked_range<size_t>(0, L), \
        [&](const tbb::blocked_range<size_t>& r) \
        { \
            for (size_t j = r.begin(); j != r.end(); ++j) \
            { \
                auto key = unique_value->GetScalar(long(j)).MoveValueUnsafe(); \
                auto& group = groups.at(key); \
\
                arrow::Datum d = arrow::compute::CallFunction( \
                                     #func, \
                                     { group[index] }, \
                                     nullptr) \
                                     .MoveValueUnsafe(); \
                result[j] = d.scalar_as<arrow::CTypeTraits<T>::ScalarType>().value; \
            } \
        }); \
\
    auto builder = arrow::CTypeTraits<T>::BuilderType(); \
    builder.AppendValues(result); \
\
std::shared_ptr<arrow::Array> data; \
builder.Finish(&data); \
\
return pd::Series(data, nullptr); \
}

#define GROUPBY_AGG(func) \
arrow::Result<pd::DataFrame> GroupBy:: func(std::vector<std::string> const& args) \
{ \
            auto schema = df.m_array->schema(); \
            auto N = groups.size(); \
        \
        auto fv = fieldVectors(args, schema); \
            arrow::ArrayDataVector arr(args.size()); \
        \
        tbb::parallel_for( \
                tbb::blocked_range<size_t>(0, args.size()), \
                [&](const tbb::blocked_range<size_t>& r) \
                { \
                    for (size_t i = r.begin(); i != r.end(); ++i) \
                    { \
                        const auto& arg = args[i]; \
                        long L = unique_value->length(); \
                        int index = schema->GetFieldIndex(arg); \
        \
                        arrow::ScalarVector result(L); \
                        tbb::parallel_for( \
                            tbb::blocked_range<size_t>(0, unique_value->length()), \
                            [&](const tbb::blocked_range<size_t>& r) \
                            { \
                                for (size_t j = r.begin(); j != r.end(); ++j) \
                                { \
                                    auto key = unique_value->GetScalar(long(j)) \
                       .MoveValueUnsafe(); \
                                    auto& group = groups.at(key); \
        \
        arrow::Datum d = arrow::compute::CallFunction( \
                                                         #func,\
                                                         { group[index] }, \
                                                         nullptr) \
                                                         .MoveValueUnsafe(); \
                                    result[j] = d.scalar(); \
                                } \
                            }); \
        \
                        auto builder = arrow::MakeBuilder(df[keyStr].dtype()).MoveValueUnsafe(); \
                            builder->AppendScalars(result); \
        \
                        std::shared_ptr<arrow::ArrayData> data; \
                            builder->FinishInternal(&data); \
                        arr[i] = data; \
                    } \
                }); \
        \
            fv.emplace_back(arrow::field(keyStr, unique_value->type())); \
                arr.push_back(unique_value->data()); \
        \
            return pd::DataFrame(arrow::schema(fv), long(N), arr); \
} \
\
arrow::Result<pd::Series> GroupBy:: func(std::string const& arg) \
{ \
            auto schema = df.m_array->schema(); \
            auto N = groups.size(); \
        \
        auto fv = schema->GetFieldByName(arg); \
        \
            long L = unique_value->length(); \
            int index = schema->GetFieldIndex(arg); \
        \
        arrow::ScalarVector result(L); \
            tbb::parallel_for( \
                tbb::blocked_range<size_t>(0, L), \
                [&](const tbb::blocked_range<size_t>& r) \
                { \
                    for (size_t j = r.begin(); j != r.end(); ++j) \
                    { \
                        auto key = unique_value->GetScalar(long(j)).MoveValueUnsafe(); \
                        auto& group = groups.at(key); \
        \
                        arrow::Datum d = arrow::compute::CallFunction( \
                                             #func, \
                                             { group[index] }, \
                                            nullptr) \
                                             .MoveValueUnsafe(); \
                        result[j] = d.scalar(); \
                    } \
                }); \
        \
            auto builder = arrow::MakeBuilder(df[keyStr].dtype()).MoveValueUnsafe();\
            builder->AppendScalars(result); \
        \
        std::shared_ptr<arrow::Array> data; \
        builder->Finish(&data); \
        \
        return pd::Series(data, nullptr); \
}