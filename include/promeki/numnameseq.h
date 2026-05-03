/**
 * @file      numnameseq.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/numname.h>

PROMEKI_NAMESPACE_BEGIN

class StringList;

/**
 * @brief Describes a NumName sequence with a head and tail range.
 * @ingroup util
 *
 * Represents a named, numbered file sequence (e.g. "image.0001.exr" through
 * "image.0100.exr") by storing the NumName pattern along with the first
 * and last sequence indices.
 */
class NumNameSeq {
        public:
                /** @brief List of NumNameSeq values. */
                using List = ::promeki::List<NumNameSeq>;

                /**
                 * @brief Parses a list of NumNameSeq objects from the string list.
                 *
                 * Any objects that can't be num names will be left in the
                 * input list.
                 *
                 * @param input The string list to parse; non-matching entries remain.
                 * @return A List of parsed NumNameSeq objects.
                 */
                static List parseList(StringList &input);

                /** @brief Default constructor. */
                NumNameSeq() = default;

                /**
                 * @brief Constructs a NumNameSeq with the given name, head, and tail.
                 * @param n The NumName pattern for this sequence.
                 * @param h The head (first) index of the sequence.
                 * @param t The tail (last) index of the sequence.
                 */
                NumNameSeq(const NumName &n, size_t h, size_t t) : _name(n), _head(h), _tail(t) {}

                /**
                 * @brief Returns the NumName for this sequence.
                 * @return A const reference to the NumName pattern.
                 */
                const NumName &name() const { return _name; }

                /**
                 * @brief Returns true if the sequence is valid.
                 * @return true if the underlying NumName is valid.
                 */
                bool isValid() const { return _name.isValid(); }

                /**
                 * @brief Returns the number of entries in the sequence.
                 * @return The count of indices from head through tail, inclusive.
                 */
                size_t length() const { return _tail - _head + 1; }

                /**
                 * @brief Returns the head (first) index of the sequence.
                 * @return The starting index.
                 */
                size_t head() const { return _head; }

                /**
                 * @brief Returns the tail (last) index of the sequence.
                 * @return The ending index.
                 */
                size_t tail() const { return _tail; }

        private:
                NumName _name;
                size_t  _head = 0;
                size_t  _tail = 0;
};

PROMEKI_NAMESPACE_END
