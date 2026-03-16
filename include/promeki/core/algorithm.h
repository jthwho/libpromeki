/**
 * @file      core/algorithm.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <algorithm>
#include <numeric>
#include <functional>
#include <promeki/core/namespace.h>
#include <promeki/core/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Returns a sorted copy of the container.
 * @ingroup core_util
 *
 * @tparam Container A container with begin()/end() and a copy constructor.
 * @param c The source container.
 * @return A sorted copy.
 */
template <typename Container>
Container sorted(Container c) {
        std::sort(c.begin(), c.end());
        return c;
}

/**
 * @brief Returns a sorted copy of the container using a custom comparator.
 * @tparam Container A container with begin()/end() and a copy constructor.
 * @tparam Compare A binary comparison function.
 * @param c The source container.
 * @param comp The comparator.
 * @return A sorted copy.
 */
template <typename Container, typename Compare>
Container sorted(Container c, Compare comp) {
        std::sort(c.begin(), c.end(), comp);
        return c;
}

/**
 * @brief Returns a copy of the container with only elements matching the predicate.
 * @tparam Container A container with begin()/end().
 * @tparam Predicate A unary predicate.
 * @param c The source container.
 * @param pred The predicate.
 * @return A filtered copy.
 */
template <typename Container, typename Predicate>
Container filtered(const Container &c, Predicate pred) {
        Container result;
        for(auto it = c.begin(); it != c.end(); ++it) {
                if(pred(*it)) result += *it;
        }
        return result;
}

/**
 * @brief Returns a container of transformed elements.
 * @tparam Container A container with begin()/end() and a value_type.
 * @tparam Transform A unary transformation function.
 * @param c The source container.
 * @param fn The transform function.
 * @return A List of transformed values.
 */
template <typename Container, typename Transform>
auto mapped(const Container &c, Transform fn) {
        using ResultType = decltype(fn(*c.begin()));
        List<ResultType> result;
        result.reserve(c.size());
        for(auto it = c.begin(); it != c.end(); ++it) {
                result += fn(*it);
        }
        return result;
}

/**
 * @brief Returns true if all elements satisfy the predicate.
 * @tparam Container A container with begin()/end().
 * @tparam Predicate A unary predicate.
 * @param c The container.
 * @param pred The predicate.
 * @return True if all elements match.
 */
template <typename Container, typename Predicate>
bool allOf(const Container &c, Predicate pred) {
        return std::all_of(c.begin(), c.end(), pred);
}

/**
 * @brief Returns true if any element satisfies the predicate.
 * @tparam Container A container with begin()/end().
 * @tparam Predicate A unary predicate.
 * @param c The container.
 * @param pred The predicate.
 * @return True if any element matches.
 */
template <typename Container, typename Predicate>
bool anyOf(const Container &c, Predicate pred) {
        return std::any_of(c.begin(), c.end(), pred);
}

/**
 * @brief Returns true if no elements satisfy the predicate.
 * @tparam Container A container with begin()/end().
 * @tparam Predicate A unary predicate.
 * @param c The container.
 * @param pred The predicate.
 * @return True if no elements match.
 */
template <typename Container, typename Predicate>
bool noneOf(const Container &c, Predicate pred) {
        return std::none_of(c.begin(), c.end(), pred);
}

/**
 * @brief Applies a callable to each element.
 * @tparam Container A container with begin()/end().
 * @tparam Callable A unary function.
 * @param c The container.
 * @param fn The function to apply.
 */
template <typename Container, typename Callable>
void forEach(const Container &c, Callable fn) {
        std::for_each(c.begin(), c.end(), fn);
}

/**
 * @brief Folds/reduces the container with a binary operation.
 * @tparam Container A container with begin()/end().
 * @tparam Init The accumulator initial value type.
 * @tparam BinaryOp A binary operation.
 * @param c The container.
 * @param init The initial value.
 * @param op The binary operation.
 * @return The accumulated result.
 */
template <typename Container, typename Init, typename BinaryOp>
Init accumulate(const Container &c, Init init, BinaryOp op) {
        return std::accumulate(c.begin(), c.end(), init, op);
}

/**
 * @brief Returns an iterator to the minimum element.
 * @tparam Container A container with begin()/end().
 * @param c The container.
 * @return Iterator to the minimum element.
 */
template <typename Container>
auto minElement(const Container &c) {
        return std::min_element(c.begin(), c.end());
}

/**
 * @brief Returns an iterator to the maximum element.
 * @tparam Container A container with begin()/end().
 * @param c The container.
 * @return Iterator to the maximum element.
 */
template <typename Container>
auto maxElement(const Container &c) {
        return std::max_element(c.begin(), c.end());
}

/**
 * @brief Returns true if the container contains the given value.
 * @tparam Container A container with begin()/end().
 * @tparam Value The value type.
 * @param c The container.
 * @param val The value to search for.
 * @return True if found.
 */
template <typename Container, typename Value>
bool contains(const Container &c, const Value &val) {
        return std::find(c.begin(), c.end(), val) != c.end();
}

PROMEKI_NAMESPACE_END
