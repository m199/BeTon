/**
 * @file TrackMatchingUtils.h
 * @brief Utility helpers for fuzzy track-title matching.
 */

#ifndef BETON_TRACK_MATCHING_UTILS_H
#define BETON_TRACK_MATCHING_UTILS_H

#include <String.h>
#include <SupportDefs.h>
#include <algorithm>
#include <ctype.h>
#include <vector>

/**
 * @class TrackMatchingUtils
 * @brief Static helper class for string similarity and metadata extraction.
 *
 * Provides utility functions for:
 * - Levenshtein distance calculation.
 * - String similarity scoring.
 * - Extracting track numbers from filenames.
 */
class TrackMatchingUtils {
public:
  /**
   * @brief Calculates the Levenshtein distance between two strings.
   *
   * The distance is the minimum number of single-character edits (insertions,
   * deletions or substitutions) required to change one string into the other.
   * Case-insensitive.
   *
   * @param s1 First string.
   * @param s2 Second string.
   * @return The edit distance.
   */
  static int LevenshteinDistance(const char *s1, const char *s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    std::vector<std::vector<int>> d(len1 + 1, std::vector<int>(len2 + 1));

    for (int i = 0; i <= len1; i++)
      d[i][0] = i;
    for (int j = 0; j <= len2; j++)
      d[0][j] = j;

    for (int i = 1; i <= len1; i++) {
      for (int j = 1; j <= len2; j++) {
        int cost = (tolower(s1[i - 1]) == tolower(s2[j - 1])) ? 0 : 1;
        d[i][j] = std::min(
            {d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
      }
    }
    return d[len1][len2];
  }

  /**
   * @brief Extracts the first sequence of digits from a filename as a track
   * number.
   *
   * Useful for guessing track numbers when metadata is missing.
   *
   * @param filename The filename to parse.
   * @return The extracted number, or 0 if none found.
   */
  static int ExtractTrackNumber(const char *filename) {
    BString s(filename);
    int32 len = s.Length();
    int32 i = 0;

    // Skip non-digits
    while (i < len && !isdigit(s[i]))
      i++;

    if (i >= len)
      return 0;

    BString numStr;
    while (i < len && isdigit(s[i])) {
      numStr += s[i];
      i++;
    }
    return atoi(numStr.String());
  }

  /**
   * @brief Calculates a normalized similarity score between two strings.
   *
   * Based on Levenshtein distance.
   *
   * @param s1 First string.
   * @param s2 Second string.
   * @return Float between 0.0 (no match) and 1.0 (perfect match).
   */
  static float Similarity(const char *s1, const char *s2) {
    int maxLen = std::max(strlen(s1), strlen(s2));
    if (maxLen == 0)
      return 1.0f;
    int dist = LevenshteinDistance(s1, s2);
    return 1.0f - (float)dist / maxLen;
  }
};

#endif // BETON_TRACK_MATCHING_UTILS_H
