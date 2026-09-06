/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
* @file string_ex.h
*
* @brief Provides the global string operation function implemented in c_utils.
*/

/**
* @defgroup StringOperation
* @{
* @brief Provides interfaces for operating strings.
*
* Include converting between uppercase and lowercase,
* string replacement, trim and split etc.
*/
#ifndef STRING_EX_H
#define STRING_EX_H

#include <string>
#include <vector>

namespace OHOS {

/**
 * @ingroup StringOperation
 * @brief Converts all letters in a string to uppercase.
 *
 * @param str Indicates the base string.
 * @return Returns a new `std::string` object after conversion.
 */
std::string UpperStr(const std::string& str);

/**
 * @ingroup StringOperation
 * @brief Converts all letters in a string to lowercase.
 *
 * @param str Indicates the base string.
 * @return Returns a new `std::string` object after conversion.
 */
std::string LowerStr(const std::string& str);

/**
 * @ingroup StringOperation
 * @brief Replaces a substring in the base string.
 *
 * @param str Indicates the substring to be replaced.
 * @param src Indicates the base string.
 * @param dst Indicates the expected substring for replacement.
 * @return Returns a new `std::string` object after replacement.
 */
std::string ReplaceStr(const std::string& str, const std::string& src, const std::string& dst);

/**
 * @ingroup StringOperation
 * @brief Trims a string indicated by `cTrim` from both ends of the base string.
 *
 * @param str Indicates the base string.
 * @param cTrim Indicates the string to trim from the base string, which is '' by default.
 * @return Returns a new `std::string` object after trimming.
 */
std::string TrimStr(const std::string& str, const char cTrim = ' ');

/**
 * @ingroup StringOperation
 * @brief Converts a decimal value to a hexadecimal string.
 *
 * @param value Indicates the decimal value to convert.
 * @param upper Specifies whether the output string is in uppercase.
 * The default value is `true`.
 * @return Returns a new `std::string` object after conversion.
 */
std::string DexToHexString(int value, bool upper = true);

/**
 * @ingroup StringOperation
 * @brief Splits a string by `sep`.
 *
 * @param str Indicates the base string.
 * @param sep Indicates the substring to be used as the separator.
 * @param strs Indicates the `std::vector` object to store the results.
 * @param canEmpty Specifies whether the output string can be an empty string.
 * The default value is `false`.
 * @param needTrim Specifies whether to remove whitespace from the output string.
 * The default value is `true`.
 */
void SplitStr(const std::string& str, const std::string& sep, std::vector<std::string>& strs,
              bool canEmpty = false, bool needTrim = true);

/**
 * @ingroup StringOperation
 * @brief Converts a value of int, double, or any other type to a string.
 *
 * @tparam T Indicates the type of the input data.
 * @param  iValue Indicates the input data.
 * @return Returns a new `std::string` object after conversion.
 */
template<class T>
inline std::string ToString(T iValue)
{
    return std::to_string(iValue);
}

/**
 * @ingroup StringOperation
 * @brief Converts a string to an int value.
 *
 * @param str Indicates the string to be converted.
 * @param value Indicates the `int` variable to store the result.
 * @return Returns `true` if the operation is successful;
 * returns `false` otherwise.
 */
bool StrToInt(const std::string& str, int& value);

/**
 * @ingroup StringOperation
 * @brief Checks whether all characters in a string are numeric.
 *
 * @param str Indicates the base string.
 * @return Returns `true` if all characters in the string are numeric;
 * returns `false` otherwise.
 */
bool IsNumericStr(const std::string& str);

/**
 * @ingroup StringOperation
 * @brief Checks whether all characters in a string are alphabetic.
 *
 * @param str Indicates the base string.
 * @return Returns `true` if all characters in the string are alphabetic;
 * returns `false` otherwise.
 */
bool IsAlphaStr(const std::string& str);

/**
 * @ingroup StringOperation
 * @brief Checks whether all characters in a string are in uppercase.
 *
 * @param str Indicates the base string.
 * @return Returns `true` if all characters in the string are in uppercase;
 * returns `false` otherwise.
 */
bool IsUpperStr(const std::string& str);

/**
 * @ingroup StringOperation
 * @brief Checks whether all characters in a string are in lowercase.
 *
 * @param str Indicates the base string.
 * @return Returns `true` if all characters in the string are in lowercase;
 * returns `false` otherwise.
 */
bool IsLowerStr(const std::string& str);

/**
 * @ingroup StringOperation
 * @brief Checks whether a string contains the specified substring.
 *
 * @param str Indicates the base string.
 * @param sub Indicates the substring.
 * @return Returns `true` if the string contains the specified substring;
 * returns `false` otherwise.
 */
bool IsSubStr(const std::string& str, const std::string& sub);

/**
 * @ingroup StringOperation
 * @brief Obtains the first substring between the substrings specified
 * by `left` and `right`.
 *
 * @param str Indicates the base string.
 * @param left Indicates the left string.
 * @param right Indicates the right string.
 * @param sub Indicates the `std::string` object to store the result string.
 * @return Returns `pos` if the operation is successful;
 * returns `string::npos` otherwise.
 */
std::string::size_type GetFirstSubStrBetween(const std::string& str, const std::string& left,
                                             const std::string& right, std::string& sub);

/**
 * @ingroup StringOperation
 * @brief Obtains all of the substrings between the substrings specified
 * by `left` and `right`.
 *
 * @param str Indicates the base string.
 * @param left Indicates the left string.
 * @param right Indicates the right string.
 * @param sub Indicates the `std::vector` object to store all the result strings.
 */
void GetSubStrBetween(const std::string& str, const std::string& left,
                      const std::string& right, std::vector<std::string>& sub);

/**
 * @ingroup StringOperation
 * @brief Checks whether two strings are equal.
 *
 * @param first Indicates the first string to check.
 * @param second Indicates the second string to check.
 * @note The string is case-insensitive.
 */
bool IsSameTextStr(const std::string& first, const std::string& second);

/**
 * @ingroup StringOperation
 * @brief Checks whether all characters in a string are ASCII encoded.
 *
 * @param str Indicates the base string.
 * @return Returns `true` if all characters in the string are ASCII encoded;
 * returns `false` otherwise.
 */
bool IsAsciiString(const std::string& str);

#ifndef IOS_PLATFORM
/**
 * @ingroup StringOperation
 * @brief Converts a string from UTF-16 to UTF-8 encoded.
 *
 * @param str16 Indicates a `std::u16string` object.
 * @return Returns an empty `std::string` object if the operation fails.
 */
std::string Str16ToStr8(const std::u16string& str16);

/**
 * @ingroup StringOperation
 * @brief Converts a string from UTF-8 to UTF-16 encoded.
 *
 * @param str Indicates a `std::string` object.
 * @return Returns an empty `std::u16string` object if the operation fails.
 */
std::u16string Str8ToStr16(const std::string& str);

/**
 * @ingroup StringOperation
 * @brief get the length of utf8 from utf16.
 *
 * @param str16 Indicates a `std::u16string` object.
 * @return Returns -1 if the str16 is empty or the result is greater than INT MAX.
 */
int GetUtf16ToUtf8Length(const std::u16string& str16);

/**
 * @ingroup StringOperation
 * @brief Converts a string from UTF-16 to UTF-8 encoded.
 *
 * @param str16 Indicates a `std::u16string` object.
 * @param buffer Converted UTF-8 encoded buffer.
 * @param bufferLen Buffer size.
 * @return Returns the length of the converted UTF-8 encoding including the terminator '\0';
 * Returns -1 if the operation fails.
 */
int Char16ToChar8(const std::u16string& str16, char *buffer, int bufferLen);
#endif
} // namespace OHOS

#endif // STRING_EX_H

/**@}*/
