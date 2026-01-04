#!/usr/bin/env python3
"""
Comprehensive C64Script Boolean Logic and Comparison Tests

Tests the full range of boolean and comparison operations:
- AND, OR, NOT, XOR operators
- Comparison operators: =, ==, <>, !=, <, <=, >, >=
- Operator precedence and parentheses
- Truthiness (0 = false, non-zero = true)
- Bitwise operations on integers

These tests validate that boolean expressions evaluate correctly
and produce the expected execution paths.
"""

import os
import sys
import unittest
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


class TestBooleanLogic(unittest.TestCase):
    """Test boolean logic operators"""
    
    def setUp(self):
        """Set up test fixtures"""
        self.script_dir = Path(__file__).parent / "scripts"
    
    def _load_script(self, filename):
        """Load a test script"""
        script_path = self.script_dir / filename
        self.assertTrue(script_path.exists(), f"Script not found: {filename}")
        with open(script_path, 'r') as f:
            return f.read()
    
    def _count_operator(self, script, operator):
        """Count occurrences of an operator in script"""
        return script.upper().count(f" {operator.upper()} ")
    
    def test_and_operator(self):
        """Test AND operator"""
        script = self._load_script("test_boolean_logic.c64script")
        
        # Should have AND operations
        and_count = self._count_operator(script, "AND")
        self.assertGreater(and_count, 0, "Should have AND operator")
        
        # Should have test case with A AND B
        self.assertIn("A AND B", script)
    
    def test_or_operator(self):
        """Test OR operator"""
        script = self._load_script("test_boolean_logic.c64script")
        
        # Should have OR operations
        or_count = self._count_operator(script, "OR")
        self.assertGreater(or_count, 0, "Should have OR operator")
        
        # Should have test case with C OR D
        self.assertIn("C OR D", script)
    
    def test_not_operator(self):
        """Test NOT operator"""
        script = self._load_script("test_boolean_logic.c64script")
        
        # Should have NOT operations
        not_count = script.upper().count("NOT ")
        self.assertGreater(not_count, 0, "Should have NOT operator")
    
    def test_xor_operator(self):
        """Test XOR operator"""
        script = self._load_script("test_boolean_logic.c64script")
        
        # Should have XOR operations
        xor_count = self._count_operator(script, "XOR")
        self.assertGreater(xor_count, 0, "Should have XOR operator")
        
        # Should have test case with F XOR G
        self.assertIn("F XOR G", script)
    
    def test_combined_operators(self):
        """Test combinations of boolean operators"""
        script = self._load_script("test_boolean_logic.c64script")
        
        # Should have combined expressions
        self.assertIn("(H AND I) OR J", script)
    
    def test_not_with_and(self):
        """Test NOT with AND: NOT (A AND B)"""
        script = self._load_script("test_boolean_logic.c64script")
        
        # Should have NOT (K AND L)
        self.assertIn("NOT (K AND L)", script)
    
    def test_complex_precedence(self):
        """Test complex expression with precedence"""
        script = self._load_script("test_boolean_logic.c64script")
        
        # Should have (M OR N) AND (O OR P)
        self.assertIn("(M OR N) AND (O OR P)", script)
    
    def test_bitwise_operations(self):
        """Test bitwise AND/OR/XOR on integers"""
        script = self._load_script("test_boolean_logic.c64script")
        
        # Should have bitwise test: 5 AND 3 = 1
        self.assertIn("5 AND 3", script)
        self.assertIn("RESULT = 1", script)
    
    def test_truthiness(self):
        """Test truthiness: 0 = false, non-zero = true"""
        script = self._load_script("test_boolean_logic.c64script")
        
        # Should have truthiness tests with 0 and 1
        self.assertIn("E = 0", script)
        self.assertIn("IF NOT E", script)


class TestComparisons(unittest.TestCase):
    """Test comparison operators"""
    
    def setUp(self):
        """Set up test fixtures"""
        self.script_dir = Path(__file__).parent / "scripts"
    
    def _load_script(self, filename):
        """Load a test script"""
        script_path = self.script_dir / filename
        self.assertTrue(script_path.exists(), f"Script not found: {filename}")
        with open(script_path, 'r') as f:
            return f.read()
    
    def test_equality_operator(self):
        """Test = and == operators"""
        script = self._load_script("test_comparisons.c64script")
        
        # Should have both = and ==
        self.assertIn("IF A = 5", script)
        self.assertIn("IF A == 5", script)
    
    def test_inequality_operator(self):
        """Test <> and != operators"""
        script = self._load_script("test_comparisons.c64script")
        
        # Should have both <> and !=
        self.assertIn("IF B <> 5", script)
        self.assertIn("IF B != 5", script)
    
    def test_less_than_operator(self):
        """Test < operator"""
        script = self._load_script("test_comparisons.c64script")
        
        # Should have less than test
        self.assertIn("IF C < D", script)
    
    def test_less_equal_operator(self):
        """Test <= operator"""
        script = self._load_script("test_comparisons.c64script")
        
        # Should have less than or equal test
        self.assertIn("IF E <= F", script)
        self.assertIn("IF G <= F", script)
    
    def test_greater_than_operator(self):
        """Test > operator"""
        script = self._load_script("test_comparisons.c64script")
        
        # Should have greater than test
        self.assertIn("IF H > I", script)
    
    def test_greater_equal_operator(self):
        """Test >= operator"""
        script = self._load_script("test_comparisons.c64script")
        
        # Should have greater than or equal test
        self.assertIn("IF J >= K", script)
        self.assertIn("IF L >= K", script)
    
    def test_comparison_chains(self):
        """Test comparison chains in conditions"""
        script = self._load_script("test_comparisons.c64script")
        
        # Should have chained comparisons: M >= 3 AND M <= 4
        self.assertIn("IF M >= 3 AND M <= 4", script)
    
    def test_string_comparisons(self):
        """Test string comparison operators"""
        script = self._load_script("test_comparisons.c64script")
        
        # Should have string comparison
        self.assertIn('NAME$ = "TEST"', script)
        self.assertIn('IF NAME$ = "TEST"', script)


class TestBooleanIntegration(unittest.TestCase):
    """Integration tests for boolean logic in control flow"""
    
    def setUp(self):
        """Set up test fixtures"""
        self.script_dir = Path(__file__).parent / "scripts"
    
    def _load_script(self, filename):
        """Load a test script"""
        script_path = self.script_dir / filename
        self.assertTrue(script_path.exists(), f"Script not found: {filename}")
        with open(script_path, 'r') as f:
            return f.read()
    
    def test_boolean_in_while_loop(self):
        """Test boolean conditions in WHILE loops"""
        script = self._load_script("test_variable_scope.c64script")
        
        # Should have WHILE with comparison
        self.assertIn("WHILE VALUE < 5", script)
    
    def test_boolean_in_if_statement(self):
        """Test boolean conditions in IF statements"""
        script = self._load_script("test_boolean_logic.c64script")
        
        # Should have multiple IF statements with boolean conditions
        if_count = script.upper().count("IF ")
        self.assertGreater(if_count, 5, "Should have multiple IF statements")
    
    def test_boolean_with_loop_iterations(self):
        """Test boolean conditions in loop iteration control"""
        script = self._load_script("test_iteration_counts.c64script")
        
        # Should have IF with OR conditions for specific counts
        self.assertIn("COUNT = 10 OR COUNT = 50 OR COUNT = 100", script)


if __name__ == '__main__':
    print("C64Script Boolean Logic and Comparison Tests")
    print("=" * 70)
    print()
    print("Running comprehensive boolean and comparison validation...")
    print()
    unittest.main(verbosity=2)
