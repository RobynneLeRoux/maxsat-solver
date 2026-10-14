#ifndef MAXSAT__PARSER_H
#define MAXSAT__PARSER_H

#include "Formula.hpp"
#include <istream>
#include <tuple>

using std::tuple, std::ifstream, std::istream;

class Parser {
public:
  static Formula parse(const string &input_file_name);
  static Formula parse_string(const string &formula_string);

private:
  static tuple<string, size_t, size_t, size_t>
  extract_header_info(istream &stream);

  static Formula parse_stream(istream &stream);

  static void parse_cnf(istream &stream, size_t &n_vars, size_t &n_clauses,
                        vector<vector<int>> &clauses, vector<size_t> &weights,
                        vector<int> &cardinality);

  static void parse_wcnf_or_wcard(istream &stream, size_t &n_vars, size_t &n_clauses,
                                  size_t &n_hard_clauses, size_t &max_weight,
                                  vector<vector<int>> &clauses,
                                  vector<size_t> &weights,
                                  vector<int> &cardinality);
};
#endif
