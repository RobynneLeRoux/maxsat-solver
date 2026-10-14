#include "maxsat/Parser.hpp"
#include "maxsat/Formula.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
#include <ranges>
#include <sstream>
#include <string>
#include <tuple>

// There will be 3 data structures
// A Vector for Weight, a Matrix for literals, and a vector for cardinality
// We can flatten this in preprocessing
// Cardinality is represented as a positive or negative int where:
// positive int (k) = at most k and,
// negative int (-k) = at least k.
// cnf and wcnf will have {-1} attached to the end of each clause to
// denote "at least 1" variable must be SAT.

using std::string, std::ifstream, std::tuple, std::istringstream, std::istream;

Formula Parser::parse_string(const string &formula_string) {
  istringstream stream(formula_string);

  return parse_stream(stream);
}

Formula Parser::parse_stream(istream &stream) {
  auto [type, n_vars, n_clauses, max_weight] =
      Parser::extract_header_info(stream);

  if (n_clauses == 0 || n_vars == 0) {
    throw std::runtime_error(
        "Error: Header states zero clauses and/or variables!");
  }

  size_t n_hard_clauses;
  vector<vector<int>> clauses;
  vector<size_t> weights;
  vector<int> cardinality;

  if (type.compare("cnf") == 0) {
    n_hard_clauses = n_clauses;
    Parser::parse_cnf(stream, n_vars, n_clauses, clauses, weights, cardinality);
  } else {
    Parser::parse_wcnf_or_wcard(stream, n_vars, n_clauses, n_hard_clauses,
                                max_weight, clauses, weights, cardinality);
  }

  if (clauses.size() != n_clauses) {
    // clauses recorded do not match header
    throw std::runtime_error("Number of clauses parsed does not match header!");
  }

  return Formula(std::move(n_vars), std::move(n_clauses),
                 std::move(n_hard_clauses), std::move(max_weight),
                 std::move(string_to_formula_type(type)), std::move(weights),
                 std::move(clauses), std::move(cardinality));
}

Formula Parser::parse(const string &filename) {
  ifstream file(filename); // open file

  if (!file.is_open()) {
    throw std::runtime_error("Error: Unable to open file!");
  }

  Formula formula = parse_stream(file);

  file.close();

  return formula;
};

// tuple to contain header info: type, n_vars, n_clauses, max_weight
tuple<string, size_t, size_t, size_t>
Parser::extract_header_info(istream &stream) {

  string line, p, type; // type refers to cnf, wcnf, or wcard
  int n_vars, n_clauses, max_weight;

  while (getline(stream, line)) { // read line by line

    if (line[0] == 'p') {
      // header line
      // cnf: p {type of file} {number of variables} {number of clauses}
      // wcnf and wcard: p {type of file} {number of variables} {number of
      // clauses} {max weight/cost}

      std::stringstream ss(line); // bind (string) line to stringstream
      ss >> p >> type;            // extract type

      // explicitly whitelist and reject anything else
      if (type != "cnf" && type != "wcnf" && type != "wcard") {
        throw std::runtime_error("Unrecognized format type: " + type);
      }

      if (type.compare("cnf") == 0) { // check if type is cnf
        ss >> n_vars >> n_clauses;
        if (ss.fail()) {
          throw std::runtime_error(
              "Malformed header: missing numeric field(s)");
        }
        max_weight = 1; // cnf will have max weight of 1
        break;
      } else { // type is wcnf or wcard
        ss >> n_vars >> n_clauses >> max_weight;
        if (ss.fail()) {
          throw std::runtime_error(
              "Malformed header: missing numeric field(s)");
        }
        break;
      }
    } else { // skip over empty, comment or other lines
      continue;
    }
  }

  if (p.compare("p") != 0) {
    throw std::runtime_error(
        "Invalid file format: missing header line starting with 'p'");
  }

  if (n_vars <= 0 || n_clauses <= 0 || max_weight <= 0) {
    throw std::runtime_error("Header contains non-positive value(s)");
  }

  return {type, n_vars, n_clauses, max_weight};
}

void Parser::parse_cnf(istream &stream, size_t &n_vars, size_t &n_clauses,
                       vector<vector<int>> &clauses, vector<size_t> &weights,
                       vector<int> &cardinality) {

  // initialize
  clauses.assign(n_clauses, vector<int>(0, 0));
  weights.assign(n_clauses, 1);      // add max weight of 1
  cardinality.assign(n_clauses, -1); // add cardinality of -1 (at least 1)

  size_t clause_idx = 0;
  int literal; // Variable (positive or negated)

  while (clause_idx < n_clauses) {
    if (stream >> literal) {
      if (literal == 0) {
        // guard against a stray/duplicate terminator advancing past n_clauses
        if (clause_idx >= n_clauses) {
          throw std::runtime_error("Unexpected extra clause terminator");
        }

        if (clauses[clause_idx].size() > 0) {
          clause_idx++;
          // advance stream pointer to skip rest of the line)
          stream.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        } else {
          n_clauses--;
        }

      } else {
        if (clause_idx >= n_clauses) {
          throw std::runtime_error("More variables than declared clauses");
        }

        if (std::abs(literal) > static_cast<int>(n_vars)) {
          throw std::runtime_error("Literal " + std::to_string(literal) +
                                   " exceeds declared number of variables");
        }
        clauses[clause_idx].push_back(literal);
      }
    } else {              // if we run into a comment or nonsense line
      if (stream.eof()) { // if we prematurely reach EOF
        throw std::runtime_error("Reached EOF before parsing all variables!");
      }
      stream.clear(); // clear error

      string junk;
      stream >> junk;
      // Decide explicitly: is `junk` a comment marker ("c...") we should
      // tolerate, or genuinely malformed data we should reject?
      if (junk.empty() || junk[0] != 'c') {
        throw std::runtime_error("Malformed token in clause data: " + junk);
      }

      // advance stream pointer to ignore the rest of the line)
      stream.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
  }

  clauses.resize(n_clauses);
  weights.resize(n_clauses);
  cardinality.resize(n_clauses);
};

void Parser::parse_wcnf_or_wcard(istream &stream, size_t &n_vars,
                                 size_t &n_clauses, size_t &n_hard_clauses,
                                 size_t &max_weight,
                                 vector<vector<int>> &clauses,
                                 vector<size_t> &weights,
                                 vector<int> &cardinality) {

  // initialize
  clauses.assign(n_clauses, std::vector<int>(0, 0));
  weights.assign(n_clauses, 0);
  cardinality.assign(n_clauses, 0);
  n_hard_clauses = 0;

  size_t clause_idx = 0;
  int literal; // Variable (positive or negated)
  string
      inequality; // <= is at most k (+ positive), >= is at least k (- negative)
  int card;       // cardinality
  vector<bool> weight_set(n_clauses, false); // flag to track weight set or not

  while (clause_idx < n_clauses) {
    if (stream >> literal) {
      if (literal == 0) {
        if (clauses[clause_idx].size() > 0) { // good path
          cardinality[clause_idx] = -1;       // "at least 1" cardinality
          clause_idx++;
          // advance stream pointer to skip rest of the line
          stream.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        } else { // bad path
          // Clause with 0 weight: Impossible
          // or empty clause (with normal weight)
          // Skip the rest of the line
          stream.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

          if (weights[clause_idx] == max_weight) {
            n_hard_clauses--; //
          }
          weight_set[clause_idx] = false;
          weights[clause_idx] = 0;
          n_clauses--;
        }

      } else if (weight_set[clause_idx]) { // this is a variable
        if (std::abs(literal) > static_cast<int>(n_vars)) {
          throw std::runtime_error("Literal " + std::to_string(literal) +
                                   " exceeds declared number of variables");
        }
        clauses[clause_idx].push_back(literal);
      } else { // this is the weight
        if (literal < 0) {
          throw std::runtime_error("Negative weight not accepted!");
        }
        weights[clause_idx] = literal;
        weight_set[clause_idx] = true;
        if (static_cast<std::size_t>(literal) == max_weight) {
          n_hard_clauses++; // count no. hard clauses
        }
      }
      // check for cardinality symbol and any comments inbetween clauses
    } else {
      stream.clear(); // clear error

      if (stream >> inequality) {
        // Lambda to check and set cardinality
        auto checkAndSetCard = [&](auto sign) {
          stream >> card;
          if (card < 0) {
            throw std::runtime_error("Negative cardinality not accepted!");
          }
          if (static_cast<std::size_t>(card) > clauses[clause_idx].size()) {
            throw std::runtime_error(
                "Cardinality must be <= number of vars in clause!");
          }
          cardinality[clause_idx] = sign * card;
          clause_idx++;
        };

        if (inequality.compare("<=") == 0) { // at most k
          checkAndSetCard(1);

        } else if (inequality.compare(">=") == 0) { // at least k
          checkAndSetCard(-1);

        } else {              // comment line or nonsense line
          if (stream.eof()) { // if we prematurely reach EOF
            throw std::runtime_error(
                "Reached EOF before parsing all variables!");
          }
          stream.clear();
          // advance stream pointer to ignore the rest of the line)
          stream.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
      } else {
        // sign extraction itself failed
        throw std::runtime_error("Reached EOF before parsing all variables!");
      }
    }
  }

  clauses.resize(n_clauses);
  weights.resize(n_clauses);
  cardinality.resize(n_clauses);
};
