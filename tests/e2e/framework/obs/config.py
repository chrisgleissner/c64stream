from __future__ import annotations
import shutil
import os
import configparser
import logging
from pathlib import Path
from typing import Optional

from ..environment import Environment

logger = logging.getLogger(__name__)

class OBSConfigManager:
    """Manages OBS Studio configuration and profiles for E2E testing."""

    def __init__(self, env: Environment):
        self.env = env
        self.obs_config_dir = Path.home() / '.config' / 'obs-studio'
        self.plugin_data_dir = self.obs_config_dir / 'plugins' / 'c64stream' / 'data'
        self._backed_up_properties = []

    def setup_plugin_data(self) -> bool:
        """Ensure plugin data directory exists."""
        if not self.plugin_data_dir.exists():
            logger.info("⚠️ Plugin data directory not found, creating it...")
            try:
                self.plugin_data_dir.mkdir(parents=True, exist_ok=True)
                logger.info("✅ Created plugin data directory")
                return True
            except Exception as e:
                logger.error(f"❌ Failed to create plugin data directory: {e}")
                return False
        return True

    def copy_e2e_properties(self) -> bool:
        """Copy E2E properties file to plugin data directory."""
        if not self.setup_plugin_data():
            return False

        # Determine source properties file
        script_dir = self.env.test_dir

        # Use local properties file for local environments to avoid CI-specific behavior
        if self.env.is_ci:
            e2e_properties = script_dir / 'properties_e2e_ci.ini'
        else:
            # Check if local properties exist, fallback to CI properties if not
            local_properties = script_dir / 'properties_e2e_local.ini'
            if local_properties.exists():
                e2e_properties = local_properties
                logger.info("📋 Using local E2E properties (non-CI environment)")
            else:
                e2e_properties = script_dir / 'properties_e2e_ci.ini'
                logger.info("⚠️ Local E2E properties not found, using CI properties")

        target_properties = self.plugin_data_dir / 'properties.ini'

        if e2e_properties.exists():
            try:
                # Backup existing properties.ini if it exists (for restoration after E2E)
                if target_properties.exists() and not self.env.is_ci:
                    backup_path = target_properties.with_suffix('.ini.e2e_backup')
                    shutil.copy2(target_properties, backup_path)
                    self._backed_up_properties.append((backup_path, target_properties))
                    logger.info(f"📦 Backed up production properties: {target_properties} -> {backup_path}")

                shutil.copy2(e2e_properties, target_properties)
                logger.info(f"✅ Copied E2E properties: {e2e_properties} -> {target_properties}")

                # Inject output_dir as save_folder for ALL runs to satisfy test expectations
                self._inject_recording_path(target_properties)

                # Apply CI-specific adjustments
                if self.env.is_ci:
                    self._apply_ci_properties(target_properties)
                    self._attempt_system_wide_properties(e2e_properties)

                return True
            except Exception as e:
                logger.error(f"❌ Failed to copy E2E properties: {e}")
                return False
        else:
            logger.error(f"❌ E2E properties file not found: {e2e_properties}")
            return False

    def _inject_recording_path(self, target_properties: Path):
        """Force the plugin to save recordings/CSVs to the test output directory."""
        try:
            save_path = str(self.env.output_dir)
            content = target_properties.read_text(encoding='utf-8')

            # Use regex to replace save_folder
            import re
            if re.search(r'^save_folder=.*$', content, flags=re.MULTILINE):
                content = re.sub(r'^save_folder=.*$', f'save_folder={save_path}', content, flags=re.MULTILINE)
            else:
                 # Fallback if key missing
                if '[recording]' in content:
                    content = content.replace('[recording]', f'[recording]\nsave_folder={save_path}')
                else:
                    content += f'\n[recording]\nsave_folder={save_path}\n'

            target_properties.write_text(content, encoding='utf-8')
            logger.info(f"📝 Configured plugin storage path: {save_path}")
        except Exception as e:
            logger.warning(f"⚠️ Failed to inject recording path: {e}")

    def _apply_ci_properties(self, target_properties: Path):
        """Apply CI-specific property overrides."""
        try:
            ci_save_folder = Path.home() / 'Documents' / 'obs-studio' / 'c64stream' / 'recordings'
            ci_save_folder.mkdir(parents=True, exist_ok=True)

            # Rewrite save_folder in the copied properties.ini
            props_text = target_properties.read_text(encoding='utf-8', errors='ignore')
            if 'save_folder=' in props_text:
                import re
                props_text = re.sub(r'^\s*save_folder\s*=.*$', f'save_folder={ci_save_folder}', props_text, flags=re.MULTILINE)
            else:
                props_text += f"\nsave_folder={ci_save_folder}\n"
            target_properties.write_text(props_text, encoding='utf-8')
            logger.info(f"🔧 CI save folder set to: {ci_save_folder}")
        except Exception as adjust_e:
            logger.warning(f"⚠️ Could not enforce CI save_folder: {adjust_e}")

    def _attempt_system_wide_properties(self, e2e_properties: Path):
        """Attempt to apply properties to system-wide installation."""
        system_data_dir = Path('/usr/share/obs/obs-plugins/c64stream')
        if system_data_dir.exists():
            try:
                sys_target = system_data_dir / 'properties.ini'
                # Only attempt if writable
                if os.access(system_data_dir, os.W_OK):
                    shutil.copy2(e2e_properties, sys_target)
                    logger.info(f"✅ Applied E2E properties to system data dir: {sys_target}")
                    # Apply CI save folder
                    try:
                        props_text = sys_target.read_text(encoding='utf-8', errors='ignore')
                        ci_save_folder = Path.home() / 'Documents' / 'obs-studio' / 'c64stream' / 'recordings'
                        if 'save_folder=' in props_text:
                            import re
                            props_text = re.sub(r'^\s*save_folder\s*=.*$', f'save_folder={ci_save_folder}', props_text, flags=re.MULTILINE)
                        else:
                            props_text += f"\nsave_folder={ci_save_folder}\n"
                        sys_target.write_text(props_text, encoding='utf-8')
                    except Exception:
                        pass
                else:
                    logger.info(f"ℹ️ System data dir not writable: {system_data_dir}")
            except Exception as se:
                logger.warning(f"⚠️ Could not apply system properties.ini: {se}")

    def restore_backup(self):
        """Restore backed up properties files."""
        for backup_path, original_path in self._backed_up_properties:
            try:
                if backup_path.exists():
                    shutil.copy2(backup_path, original_path)
                    backup_path.unlink()
                    logger.info(f"♻️ Restored production properties: {original_path}")
                else:
                    logger.warning(f"⚠️ Backup not found: {backup_path}")
            except Exception as e:
                logger.error(f"❌ Failed to restore {original_path}: {e}")
        self._backed_up_properties.clear()

    def create_obs_profile(self, video_format: str, scenario_overrides_dir: Optional[Path] = None) -> Path:
        """Copy clean OBS configuration and apply overrides."""
        logger.info("Setting up OBS configuration from baseline")

        # Remove the basic directory to ensure completely clean state
        basic_dir = self.obs_config_dir / 'basic'
        if basic_dir.exists():
            logger.info(f"Removing existing OBS basic config: {basic_dir}")
            shutil.rmtree(basic_dir)

        self.obs_config_dir.mkdir(parents=True, exist_ok=True)

        config_source = self.env.test_dir / 'config' / 'obs-studio'
        if not config_source.exists():
            raise RuntimeError(f"Baseline OBS config not found: {config_source}")

        logger.info(f"Copying baseline config from {config_source} to {self.obs_config_dir}")
        for item in config_source.iterdir():
            if item.is_dir():
                shutil.copytree(item, self.obs_config_dir / item.name)
            else:
                shutil.copy2(item, self.obs_config_dir / item.name)

        if scenario_overrides_dir and scenario_overrides_dir.exists():
            try:
                self._apply_scenario_overrides(self.obs_config_dir, scenario_overrides_dir)
                logger.info(f"✅ Applied scenario overrides from {scenario_overrides_dir}")
            except Exception as e:
                logger.warning(f"⚠️ Failed to apply scenario overrides from {scenario_overrides_dir}: {e}")

        # Replace variables
        self._replace_config_variables(self.obs_config_dir, video_format)
        self._cleanup_obs_state_files(self.obs_config_dir)

        logger.info("✅ OBS configuration copied from baseline")
        return self.obs_config_dir / 'basic' / 'profiles' / 'C64StreamTest'

    def _apply_scenario_overrides(self, obs_config_dir: Path, overrides_dir: Path):
        """Copy/Merge scenario override files."""
        for root, dirs, files in os.walk(overrides_dir):
            rel = Path(root).relative_to(overrides_dir)
            dest_root = obs_config_dir / rel
            dest_root.mkdir(parents=True, exist_ok=True)
            for fname in files:
                src = Path(root) / fname
                dst = dest_root / fname
                try:
                    if src.suffix.lower() == ".ini" and dst.exists():
                        if self._merge_ini_override(dst, src):
                            logger.info(f"  ↳ Override (merged): {rel / fname}")
                        else:
                            shutil.copy2(src, dst)
                            logger.info(f"  ↳ Override: {rel / fname}")
                    else:
                        shutil.copy2(src, dst)
                        logger.info(f"  ↳ Override: {rel / fname}")
                except Exception as e:
                    logger.warning(f"  ⚠️ Could not copy override {src}: {e}")

    def _merge_ini_override(self, dst: Path, src: Path) -> bool:
        """Merge INI override entries."""
        parser = configparser.ConfigParser(interpolation=None)
        parser.optionxform = str
        override = configparser.ConfigParser(interpolation=None)
        override.optionxform = str
        try:
            parser.read(dst, encoding="utf-8")
            override.read(src, encoding="utf-8")
            for section in override.sections():
                if not parser.has_section(section):
                    parser.add_section(section)
                for key, value in override.items(section):
                    parser.set(section, key, value)
            with open(dst, "w", encoding="utf-8") as f:
                parser.write(f, space_around_delimiters=False)
            return True
        except (configparser.Error, OSError) as e:
            logger.warning(f"  ⚠️ Could not merge INI override {src}: {e}")
            return False

    def _replace_config_variables(self, obs_config_dir: Path, video_format: str):
        """Replace variables like $FPS, $OUTPUT_DIR in OBS config."""
        if video_format == 'PAL':
            fps_num = '401'
            fps_den = '8'
            fps_float = '50.125'
        else:  # NTSC
            fps_num = '29913'
            fps_den = '500'
            fps_float = '59.826'

        variables = {
            '$OUTPUT_DIR': str(self.env.output_dir),
            '$FPS': fps_float,
            '$FPS_NUM': fps_num,
            '$FPS_DEN': fps_den,
            '$FPS_COMMON': ('50 PAL' if video_format == 'PAL' else '60'),
        }

        basic_ini = obs_config_dir / 'basic' / 'profiles' / 'C64StreamTest' / 'basic.ini'
        if basic_ini.exists():
            content = basic_ini.read_text()
            for key in sorted(variables.keys(), key=len, reverse=True):
                content = content.replace(key, variables[key])
            basic_ini.write_text(content)
            logger.info(f"Updated configuration variables in {basic_ini}")

    def _cleanup_obs_state_files(self, obs_config_dir: Path):
        """Clean up OBS state files that can trigger popup dialogs."""
        import glob
        state_patterns = [
            str(obs_config_dir / 'safe_mode'),
            str(obs_config_dir / '.safe_mode'),
            str(obs_config_dir / 'crashed'),
            str(obs_config_dir / '.crashed'),
            str(obs_config_dir / 'basic/crashed'),
            str(obs_config_dir / 'plugin_config/.safe_mode*'),
            '/tmp/obs-safe-mode-*',
            '/tmp/.obs-crashed*'
        ]

        cleaned_count = 0
        for pattern in state_patterns:
            for state_file in glob.glob(pattern):
                try:
                    state_path = Path(state_file)
                    if state_path.is_dir():
                        shutil.rmtree(state_path)
                    else:
                        state_path.unlink()
                    cleaned_count += 1
                except (OSError, IOError):
                    pass

        if cleaned_count > 0:
            logger.info(f"Cleaned up {cleaned_count} OBS state files")

        # Also patch global.ini to prevent crash dialogs
        global_ini = self.obs_config_dir / 'global.ini'
        if not global_ini.exists():
            global_ini.parent.mkdir(parents=True, exist_ok=True)
            global_ini.write_text("[General]\nCleanShutdown=true\n", encoding='utf-8')
            logger.info("✅ Created global.ini with CleanShutdown=true")
        else:
            try:
                content = global_ini.read_text(encoding='utf-8')
                if '[General]' in content:
                    # check if CleanShutdown is present
                    import re
                    if not re.search(r'CleanShutdown=', content):
                        content = content.replace('[General]', '[General]\nCleanShutdown=true')
                        global_ini.write_text(content, encoding='utf-8')
                        logger.info("✅ Patched global.ini with CleanShutdown=true")
                else:
                    # No [General] section, append it
                    content += "\n[General]\nCleanShutdown=true\n"
                    global_ini.write_text(content, encoding='utf-8')
                    logger.info("✅ Appended CleanShutdown to global.ini")
            except Exception as e:
                logger.warning(f"Failed to patch global.ini: {e}")
