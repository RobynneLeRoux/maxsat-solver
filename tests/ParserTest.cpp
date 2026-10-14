#include "catch2/catch_amalgamated.hpp"
#include "maxsat/Parser.hpp"
#include "maxsat/Formula.hpp"
#include <sstream>

// =====================================================================
// HEADER PARSING
// =====================================================================

TEST_CASE("Header: missing entirely", "[header]") {
    REQUIRE_THROWS(Parser::parse_string("1 -2 0\n"));
}

TEST_CASE("Header: 'p' line with no fields at all", "[header]") {
    REQUIRE_THROWS(Parser::parse_string("p\n1 -2 0\n"));
}

TEST_CASE("Header: cnf missing n_clauses field", "[header]") {
    std::string data = 
        "p cnf 3\n"
        "1 -2 0\n";

    REQUIRE_THROWS(Parser::parse_string(data));
}

TEST_CASE("Header: wcnf missing max_weight field", "[header]") {
    std::string data = 
        "p wcnf 3 2\n"
        "10 1 2 0\n"
        "5 -1 3 0\n";

    REQUIRE_THROWS(Parser::parse_string(data));
}

TEST_CASE("Header: negative n_vars wraps to huge size_t", "[header]") {
    std::string data = 
        "p cnf -1 2\n"
        "1 -2 0\n"
        "2 0\n";

    REQUIRE_THROWS(Parser::parse_string(data));
}

TEST_CASE("Header: type string is case-sensitive", "[header]") {
    std::string data = 
        "p CNF 3 2\n"
        "1 -2 0\n"
        "-1 3 0\n";

    REQUIRE_THROWS(Parser::parse_string(data));
}

TEST_CASE("Header: reject unknown type", "[header]") {
    std::string data = 
        "p xyz 3 2 10\n"
        "10 1 -2 0\n"
        "5 -1 3 0\n";
    
    REQUIRE_THROWS(Parser::parse_string(data));
}

TEST_CASE("Header: blank lines and comments before header", "[header]") {
    std::string data =
        "\n"
        "\n"
        "c leading comment\n"
        "p cnf 2 1\n"
        "1 -2 0\n";

    Formula formula = Parser::parse_string(data);
    REQUIRE(formula.n_vars == 2);
    REQUIRE(formula.n_clauses == 1);
}

TEST_CASE("Header: extra whitespace / trailing fields tolerated", "[header]") {
    std::string data = 
        "p cnf 3   2   \n"
        "1 -2 0\n"
        "-1 3 0\n";

    Formula formula = Parser::parse_string(data);
    REQUIRE(formula.n_vars == 3);
    REQUIRE(formula.n_clauses == 2);
}

// =====================================================================
// CNF EDGE CASES
// =====================================================================

TEST_CASE("CNF: single clause, single literal", "[cnf]") {
    std::string data = 
        "p cnf 1 1\n"
        "1 0\n";

    Formula formula = Parser::parse_string(data);
    REQUIRE(formula.clauses[0] == std::vector<int>{1});
}

TEST_CASE("CNF: no trailing newline after last clause", "[cnf]") {
    std::string data = 
        "p cnf 2 1\n"
        "1 -2 0";  // no trailing \n

    Formula formula = Parser::parse_string(data);
    REQUIRE(formula.clauses[0] == std::vector<int>{1, -2});
}

TEST_CASE("CNF: missing terminating 0 on last clause throws", "[cnf]") {
    std::string data = 
        "p cnf 2 1\n"
        "1 -2\n";  

    REQUIRE_THROWS(Parser::parse_string(data));
}

TEST_CASE("CNF: fewer clauses than header claims throws", "[cnf]") {
    std::string data = 
        "p cnf 3 2\n"
        "1 -2 0\n";  // header says 2, only 1 given

    REQUIRE_THROWS(Parser::parse_string(data));
}

TEST_CASE("CNF: literal exceeding declared n_vars throws", "[cnf]") {
    std::string data = 
        "p cnf 2 1\n"
        "1 99 0\n";

    REQUIRE_THROWS(Parser::parse_string(data));
}

TEST_CASE("CNF: empty clause (bare 0, no literals) silently removes clause", "[cnf]") {
    std::string data = 
        "p cnf 2 2\n"
        "0\n"
        "1 -2 0\n";

    Formula formula = Parser::parse_string(data);
    REQUIRE(formula.n_clauses == 1);
    REQUIRE(formula.clauses[0] == std::vector<int>{1, -2});
    REQUIRE(formula.weights == std::vector<size_t>{1});
    REQUIRE(formula.cardinality == std::vector<int>{-1});
}

TEST_CASE("CNF: duplicate terminating 0 skips", "[cnf]") {
    std::string data = 
        "p cnf 3 2\n"
        "1 -2 0 0\n"
        "-1 3 0\n";
    
    Formula formula = Parser::parse_string(data);
    REQUIRE(formula.n_clauses == 2);
    REQUIRE(formula.clauses[0] == std::vector<int>{1, -2});
    REQUIRE(formula.clauses[1] == std::vector<int>{-1, 3});
    REQUIRE(formula.weights == std::vector<size_t>{1, 1});
    REQUIRE(formula.cardinality == std::vector<int>{-1, -1});
}

TEST_CASE("CNF: bad token mid-clause throws", "[cnf]") {
    std::string data = 
        "p cnf 2 2\n"
        "1 -2 x 0\n"
        "3 0\n";

    REQUIRE_THROWS(Parser::parse_string(data));
}

TEST_CASE("CNF: duplicate/tautological literals not deduplicated", "[cnf]") {
    std::string data = 
        "p cnf 2 1\n"
        "1 1 -1 0\n";

    Formula formula = Parser::parse_string(data);
    // Confirms no dedup/tautology detection happens - literals pass through as-is.
    REQUIRE(formula.clauses[0] == std::vector<int>{1, 1, -1});
}

// =====================================================================
// WCNF / WCARD EDGE CASES
// =====================================================================

TEST_CASE("WCNF: single-literal hard clause", "[wcnf]") {
    std::string data = 
        "p wcnf 1 1 10\n"
        "10 1 0\n";

    Formula formula = Parser::parse_string(data);
    REQUIRE(formula.n_hard_clauses == 1);
    REQUIRE(formula.clauses[0] == std::vector<int>{1});
}

TEST_CASE("WCNF: clause with weight of exactly 0 gets ignored", "[wcnf]") {
    std::string data = 
        "p wcnf 2 2 10\n"
        "10 1 2 0\n"
        "0 -1 2 0\n";

    Formula formula = Parser::parse_string(data);
    REQUIRE(formula.n_clauses == 1);
    REQUIRE(formula.n_hard_clauses == 1);
    
    REQUIRE(formula.weights == std::vector<size_t>{10});
    REQUIRE(formula.cardinality == std::vector<int>{-1});
    REQUIRE(formula.clauses[0] == std::vector<int>{1, 2});

}

TEST_CASE("WCNF: multiple consecutive empty clauses collapse correctly", "[wcnf]") {
    std::string data = 
        "p wcnf 2 3 10\n"
        "10 0\n"
        "10 0\n"
        "5 1 0\n";

    Formula formula = Parser::parse_string(data);
    REQUIRE(formula.n_clauses == 1);
    REQUIRE(formula.clauses[0] == std::vector<int>{1});
    REQUIRE(formula.weights[0] == 5);
    REQUIRE(formula.n_hard_clauses == 0);
}

TEST_CASE("WCNF: empty clause is also the last clause", "[wcnf]") {
    std::string data = 
        "p wcnf 2 2 10\n"
        "10 1 2 0\n"
        "5 0\n";

    Formula formula = Parser::parse_string(data);
    REQUIRE(formula.n_clauses == 1);
    REQUIRE(formula.clauses[0] == std::vector<int>{1, 2});
}

TEST_CASE("WCARD: cardinality cannot be larger than clause size", "[wcard]") {
    std::string data = 
        "p wcard 2 1 10\n"
        "10 1 2 <= 5\n";
    REQUIRE_THROWS(Parser::parse_string(data));
}

TEST_CASE("WCARD: negative cardinality is not allowed", "[wcard]") {
    std::string data = 
        "p wcard 2 1 10\n"
        "10 1 2 >= -3\n";
    REQUIRE_THROWS(Parser::parse_string(data));
}

TEST_CASE("WCARD: comment line between cardinality clauses", "[wcard]") {
    std::string data =
        "p wcard 3 2 10\n"
        "10 1 2 3 <= 2\n"
        "c a comment between clauses\n"
        "5 -1 -2 >= 1\n";
    Formula formula = Parser::parse_string(data);
    REQUIRE(formula.cardinality[0] == 2);
    REQUIRE(formula.cardinality[1] == -1);
}

TEST_CASE("WCNF/WCARD: truncated file ending mid-token throws", "[wcnf]") {
    std::string data = 
        "p wcnf 2 2 10\n"
        "10 1 2 0\n"
        "5 -1";  // trailing junk, no 0
    REQUIRE_THROWS(Parser::parse_string(data));
}

// =====================================================================
// DEGENERATE / STRUCTURAL CASES
// =====================================================================

TEST_CASE("Zero variables in header throws", "[header]") {
    REQUIRE_THROWS(Parser::parse_string("p cnf 0 1\n1 0\n"));
}

TEST_CASE("Zero clauses in header throws", "[header]") {
    REQUIRE_THROWS(Parser::parse_string("p cnf 3 0\n"));
}

TEST_CASE("Whitespace-only input throws", "[header]") {
    REQUIRE_THROWS(Parser::parse_string("   \n\n\t\n"));
}

TEST_CASE("Empty string input throws", "[header]") {
    REQUIRE_THROWS(Parser::parse_string(""));
}

/*
TEST_CASE("parse() on nonexistent file throws", "[file]") {
        REQUIRE_THROWS(Parser::parse("/nonexistent/path/does_not_exist.cnf"));

}
*/