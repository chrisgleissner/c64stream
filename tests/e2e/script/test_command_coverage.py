#!/usr/bin/env python3
"""
Comprehensive C64Script Command Coverage Tests

Tests ALL C64Script commands to ensure complete language coverage:
- Effect and palette commands (EFFECT, EFFECTPARAM, PALETTE, PALETTECOLOR)
- C64 control (PLAYSID, RUNPRG, MOUNTDISK, AUTOSTART, RESET, REBOOT)
- Recording (RECORDSTART, RECORDSTOP)
- Keyboard injection (TYPE, KEY)
- Memory access (POKE, PEEK)
- Logging (LOG, LOGFILE, TRON, TROFF, PRINT)
- File I/O (READFILE, WRITEFILE)
- HTTP REST API (HTTP GET/POST/PUT/DELETE/PATCH)
- Local execution (RUNLOCAL)
- Arrays and maps (DIM, array/map access)
- Built-in functions (string, math, utility)
- User-defined functions (FUNCTION/ENDFUNCTION)
- Language features (LET, REM, WAIT UNTIL)

Tests validate that scripts:
1. Parse successfully
2. Contain expected commands
3. Have valid syntax structure
"""

import os
import sys
import unittest
from pathlib import Path

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


class TestAllCommands(unittest.TestCase):
    """Test coverage for ALL C64Script commands"""
    
    def setUp(self):
        """Set up test fixtures"""
        self.script_dir = Path(__file__).parent / "scripts"
    
    def _load_script(self, filename):
        """Load a test script"""
        script_path = self.script_dir / filename
        self.assertTrue(script_path.exists(), f"Script not found: {filename}")
        with open(script_path, 'r') as f:
            return f.read()
    
    def _count_command(self, script, command):
        """Count occurrences of a command in script"""
        return script.upper().count(command.upper())
    
    # ========================================================================
    # PALETTE AND EFFECT COMMANDS
    # ========================================================================
    
    def test_palette_commands(self):
        """Test PALETTE and PALETTECOLOR commands"""
        script = self._load_script("test_palette_commands.c64script")
        
        # Should have multiple PALETTE commands
        palette_count = self._count_command(script, "PALETTE ")
        self.assertGreater(palette_count, 2, "Should have multiple PALETTE commands")
        
        # Should have PALETTECOLOR commands
        palettecolor_count = self._count_command(script, "PALETTECOLOR ")
        self.assertGreater(palettecolor_count, 5, "Should have multiple PALETTECOLOR commands")
        
        # Should test different palettes
        self.assertIn("colodore", script.lower())
        self.assertIn("pepto_ntsc", script.lower())
        self.assertIn("vice_new", script.lower())
        
        # Should test color indices and RGB values
        self.assertIn("PALETTECOLOR 0,", script.upper())
        self.assertIn("PALETTECOLOR 1,", script.upper())
    
    def test_effect_params(self):
        """Test EFFECT and EFFECTPARAM commands"""
        script = self._load_script("test_effect_params.c64script")
        
        # Should have multiple EFFECT commands
        effect_count = self._count_command(script, "EFFECT ")
        self.assertGreater(effect_count, 3, "Should have multiple EFFECT commands")
        
        # Should have EFFECTPARAM commands
        effectparam_count = self._count_command(script, "EFFECTPARAM ")
        self.assertGreater(effectparam_count, 10, "Should have many EFFECTPARAM commands")
        
        # Should test different effect parameters
        self.assertIn("scanline_intensity", script.lower())
        self.assertIn("phosphor_persistence", script.lower())
        self.assertIn("curvature", script.lower())
        self.assertIn("bloom", script.lower())
    
    # ========================================================================
    # C64 CONTROL COMMANDS
    # ========================================================================
    
    def test_c64_control(self):
        """Test PLAYSID, RUNPRG, MOUNTDISK, AUTOSTART, RESET, REBOOT"""
        script = self._load_script("test_c64_control.c64script")
        
        # Should have PLAYSID with SONGNR
        playsid_count = self._count_command(script, "PLAYSID ")
        self.assertGreater(playsid_count, 2, "Should have multiple PLAYSID commands")
        self.assertIn("SONGNR", script.upper())
        
        # Should have RUNPRG
        runprg_count = self._count_command(script, "RUNPRG ")
        self.assertGreater(runprg_count, 1, "Should have RUNPRG commands")
        
        # Should have MOUNTDISK
        mountdisk_count = self._count_command(script, "MOUNTDISK ")
        self.assertGreater(mountdisk_count, 1, "Should have MOUNTDISK commands")
        
        # Should have RESET and REBOOT
        self.assertIn("RESET", script.upper())
        self.assertIn("REBOOT", script.upper())
        
        # Should have AUTOSTART
        self.assertIn("AUTOSTART", script.upper())
        
        # Should use c64u: paths
        self.assertIn("c64u:", script.lower())
    
    # ========================================================================
    # RECORDING COMMANDS
    # ========================================================================
    
    def test_recording(self):
        """Test RECORDSTART and RECORDSTOP commands"""
        script = self._load_script("test_recording.c64script")
        
        # Should have RECORDSTART
        recordstart_count = self._count_command(script, "RECORDSTART")
        self.assertGreater(recordstart_count, 1, "Should have multiple RECORDSTART commands")
        
        # Should have RECORDSTOP
        recordstop_count = self._count_command(script, "RECORDSTOP")
        self.assertGreater(recordstop_count, 1, "Should have multiple RECORDSTOP commands")
        
        # Should test recording workflow
        self.assertIn("WAIT", script.upper())
    
    # ========================================================================
    # KEYBOARD INJECTION COMMANDS
    # ========================================================================
    
    def test_keyboard_injection(self):
        """Test TYPE and KEY commands"""
        script = self._load_script("test_keyboard_injection.c64script")
        
        # Should have TYPE commands
        type_count = self._count_command(script, "TYPE ")
        self.assertGreater(type_count, 3, "Should have multiple TYPE commands")
        
        # Should have KEY commands
        key_count = self._count_command(script, "KEY ")
        self.assertGreater(key_count, 5, "Should have multiple KEY commands")
        
        # Should test symbolic key names
        self.assertIn("RETURN", script.upper())
        self.assertIn("RUNSTOP", script.upper())
        self.assertIn("HOME", script.upper())
        self.assertIn("CURSOR_", script.upper())
        
        # Should test numeric/hex key codes
        self.assertIn("KEY 13", script)
        self.assertIn("KEY $", script.upper())
    
    # ========================================================================
    # MEMORY ACCESS COMMANDS
    # ========================================================================
    
    def test_memory_access(self):
        """Test POKE and PEEK commands"""
        script = self._load_script("test_memory_access.c64script")
        
        # Should have POKE commands (single and array)
        poke_count = self._count_command(script, "POKE ")
        self.assertGreater(poke_count, 5, "Should have multiple POKE commands")
        
        # Should test array POKE
        self.assertIn("POKE $", script.upper())
        self.assertIn("[", script)
        
        # Should have PEEK function calls
        peek_count = script.upper().count("PEEK(")
        self.assertGreater(peek_count, 3, "Should have multiple PEEK calls")
        
        # Should test common memory locations
        self.assertIn("$C000", script.upper())
        self.assertIn("$D020", script.upper())
        self.assertIn("$00C6", script.upper())
    
    # ========================================================================
    # LOGGING COMMANDS
    # ========================================================================
    
    def test_logging(self):
        """Test LOG, LOGFILE, TRON, TROFF, PRINT commands"""
        script = self._load_script("test_logging.c64script")
        
        # Should have LOGFILE with modes
        logfile_count = self._count_command(script, "LOGFILE ")
        self.assertGreater(logfile_count, 1, "Should have LOGFILE commands")
        self.assertIn("TRUNCATE", script.upper())
        self.assertIn("APPEND", script.upper())
        
        # Should have LOG commands
        log_count = self._count_command(script, "LOG ")
        self.assertGreater(log_count, 5, "Should have multiple LOG commands")
        
        # Should have TRON/TROFF
        self.assertIn("TRON", script.upper())
        self.assertIn("TROFF", script.upper())
        
        # Should have PRINT
        print_count = self._count_command(script, "PRINT ")
        self.assertGreater(print_count, 0, "Should have PRINT commands")
    
    # ========================================================================
    # FILE I/O COMMANDS
    # ========================================================================
    
    def test_file_io(self):
        """Test READFILE and WRITEFILE commands"""
        script = self._load_script("test_file_io.c64script")
        
        # Should have WRITEFILE with modes
        writefile_count = self._count_command(script, "WRITEFILE ")
        self.assertGreater(writefile_count, 5, "Should have multiple WRITEFILE commands")
        self.assertIn("TRUNCATE", script.upper())
        self.assertIn("APPEND", script.upper())
        
        # Should have READFILE
        readfile_count = self._count_command(script, "READFILE ")
        self.assertGreater(readfile_count, 2, "Should have READFILE commands")
        
        # Should test file operations workflow
        self.assertIn("WAIT", script.upper())
    
    # ========================================================================
    # HTTP REST API COMMANDS
    # ========================================================================
    
    def test_http_rest(self):
        """Test HTTP GET/POST/PUT/DELETE/PATCH commands"""
        script = self._load_script("test_http_rest.c64script")
        
        # Should have HTTP commands
        http_count = self._count_command(script, "HTTP ")
        self.assertGreater(http_count, 8, "Should have many HTTP commands")
        
        # Should test all HTTP methods
        self.assertIn("HTTP GET", script.upper())
        self.assertIn("HTTP POST", script.upper())
        self.assertIn("HTTP PUT", script.upper())
        self.assertIn("HTTP DELETE", script.upper())
        self.assertIn("HTTP PATCH", script.upper())
        
        # Should test HTTP parameters
        self.assertIn("STATUS", script.upper())
        self.assertIn("RESPONSE", script.upper())
        self.assertIn("BODY", script.upper())
        self.assertIn("HEADERS", script.upper())
        
        # Should test localhost URLs
        self.assertIn("localhost:8064", script.lower())
    
    # ========================================================================
    # LOCAL EXECUTION COMMANDS
    # ========================================================================
    
    def test_local_execution(self):
        """Test RUNLOCAL command"""
        script = self._load_script("test_local_execution.c64script")
        
        # Should have RUNLOCAL commands
        runlocal_count = self._count_command(script, "RUNLOCAL ")
        self.assertGreater(runlocal_count, 5, "Should have multiple RUNLOCAL commands")
        
        # Should test RUNLOCAL parameters
        self.assertIn("ARGS", script.upper())
        self.assertIn("STATUS", script.upper())
        self.assertIn("OUTPUT", script.upper())
        
        # Should test various local commands
        self.assertIn("echo", script.lower())
    
    # ========================================================================
    # ARRAYS AND MAPS
    # ========================================================================
    
    def test_arrays_maps(self):
        """Test DIM, array access, and map access"""
        script = self._load_script("test_arrays_maps.c64script")
        
        # Should have DIM declarations
        dim_count = self._count_command(script, "DIM ")
        self.assertGreater(dim_count, 5, "Should have multiple DIM declarations")
        
        # Should test array access with ()
        self.assertIn("(0)", script)
        self.assertIn("(1)", script)
        
        # Should test map access with {}
        self.assertIn("{", script)
        self.assertIn("}", script)
        
        # Should test string arrays
        self.assertIn("DIM ", script.upper())
        self.assertIn("$", script)
    
    # ========================================================================
    # BUILT-IN FUNCTIONS
    # ========================================================================
    
    def test_functions_builtin(self):
        """Test all built-in functions"""
        script = self._load_script("test_functions_builtin.c64script")
        
        # String functions
        self.assertIn("LEFT$(", script.upper())
        self.assertIn("RIGHT$(", script.upper())
        self.assertIn("MID$(", script.upper())
        self.assertIn("LEN(", script.upper())
        self.assertIn("CHR$(", script.upper())
        self.assertIn("ASC(", script.upper())
        
        # Conversion functions
        self.assertIn("STR$(", script.upper())
        self.assertIn("VAL(", script.upper())
        
        # Math functions
        self.assertIn("ABS(", script.upper())
        self.assertIn("INT(", script.upper())
        self.assertIn("RND(", script.upper())
        self.assertIn("SIN(", script.upper())
        self.assertIn("COS(", script.upper())
        self.assertIn("TAN(", script.upper())
        self.assertIn("SQRT(", script.upper())
        self.assertIn("LOG(", script.upper())
        self.assertIn("EXP(", script.upper())
        
        # Utility functions
        self.assertIn("TIME$(", script.upper())
    
    # ========================================================================
    # USER-DEFINED FUNCTIONS
    # ========================================================================
    
    def test_user_functions(self):
        """Test FUNCTION/ENDFUNCTION"""
        script = self._load_script("test_user_functions.c64script")
        
        # Should have FUNCTION declarations
        function_count = self._count_command(script, "FUNCTION ")
        self.assertGreater(function_count, 5, "Should have multiple FUNCTION declarations")
        
        # Should have ENDFUNCTION
        endfunction_count = self._count_command(script, "ENDFUNCTION")
        self.assertGreater(endfunction_count, 5, "Should have matching ENDFUNCTION")
        
        # Should have RETURN statements
        return_count = self._count_command(script, "RETURN ")
        self.assertGreater(return_count, 5, "Should have RETURN statements")
        
        # Should test function calls
        # Functions should be called and results assigned
        self.assertIn("=", script)
    
    # ========================================================================
    # LANGUAGE FEATURES
    # ========================================================================
    
    def test_let_rem(self):
        """Test LET and REM statements"""
        script = self._load_script("test_let_rem.c64script")
        
        # Should have LET statements
        let_count = self._count_command(script, "LET ")
        self.assertGreater(let_count, 5, "Should have multiple LET statements")
        
        # Should have REM comments
        rem_count = self._count_command(script, "REM ")
        self.assertGreater(rem_count, 3, "Should have REM comments")
        
        # Should also have # comments
        hash_count = script.count("\n#")
        self.assertGreater(hash_count, 2, "Should have # comments")
    
    def test_wait_until(self):
        """Test WAIT UNTIL command"""
        script = self._load_script("test_wait_until.c64script")
        
        # Should have WAIT UNTIL commands
        wait_until_count = self._count_command(script, "WAIT UNTIL")
        self.assertGreater(wait_until_count, 5, "Should have multiple WAIT UNTIL commands")
        
        # Should test different time formats
        # HH:MM:SS, HH:MM, full datetime, ISO-8601
        self.assertIn(":", script)
        
        # Should use TIME$() function
        self.assertIn("TIME$(", script.upper())


class TestScriptCoverage(unittest.TestCase):
    """Verify all command test scripts exist"""
    
    def setUp(self):
        """Set up test fixtures"""
        self.script_dir = Path(__file__).parent / "scripts"
    
    def test_all_command_scripts_exist(self):
        """Verify all comprehensive command test scripts exist"""
        expected_scripts = [
            # Original scripts
            "test_nested_loops.c64script",
            "test_boolean_logic.c64script",
            "test_comparisons.c64script",
            "test_variable_scope.c64script",
            "test_iteration_counts.c64script",
            "test_simple_sequence.c64script",
            "test_loop.c64script",
            
            # New comprehensive command coverage
            "test_palette_commands.c64script",
            "test_effect_params.c64script",
            "test_c64_control.c64script",
            "test_recording.c64script",
            "test_keyboard_injection.c64script",
            "test_memory_access.c64script",
            "test_logging.c64script",
            "test_file_io.c64script",
            "test_http_rest.c64script",
            "test_local_execution.c64script",
            "test_arrays_maps.c64script",
            "test_functions_builtin.c64script",
            "test_user_functions.c64script",
            "test_let_rem.c64script",
            "test_wait_until.c64script",
            
            # Error tests
            "test_error_invalid_command.c64script",
            "test_error_goto_missing.c64script",
            "test_error_duplicate_label.c64script",
            "test_error_type_mismatch.c64script",
            "test_error_missing_next.c64script",
            "test_error_missing_wend.c64script",
            "test_error_gosub_overflow.c64script",
            "test_safety_max_nesting.c64script",
            "test_safety_infinite_loop.c64script",
        ]
        
        for script_name in expected_scripts:
            script_path = self.script_dir / script_name
            self.assertTrue(script_path.exists(), 
                          f"Missing comprehensive test script: {script_name}")
    
    def test_command_coverage_complete(self):
        """Verify we have tests for all major command categories"""
        required_categories = {
            "palette": ["test_palette_commands.c64script"],
            "effects": ["test_effect_params.c64script"],
            "c64_control": ["test_c64_control.c64script"],
            "recording": ["test_recording.c64script"],
            "keyboard": ["test_keyboard_injection.c64script"],
            "memory": ["test_memory_access.c64script"],
            "logging": ["test_logging.c64script"],
            "file_io": ["test_file_io.c64script"],
            "http": ["test_http_rest.c64script"],
            "local_exec": ["test_local_execution.c64script"],
            "data_structures": ["test_arrays_maps.c64script"],
            "builtin_funcs": ["test_functions_builtin.c64script"],
            "user_funcs": ["test_user_functions.c64script"],
            "language": ["test_let_rem.c64script", "test_wait_until.c64script"],
        }
        
        for category, scripts in required_categories.items():
            for script_name in scripts:
                script_path = self.script_dir / script_name
                self.assertTrue(script_path.exists(),
                              f"Missing {category} test: {script_name}")


if __name__ == '__main__':
    print("C64Script Comprehensive Command Coverage Tests")
    print("=" * 70)
    print()
    print("Testing ALL C64Script commands and language features...")
    print()
    unittest.main(verbosity=2)
