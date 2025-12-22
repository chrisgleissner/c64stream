#!/usr/bin/env zsh

builtin emulate -L zsh
setopt EXTENDED_GLOB
setopt ERR_EXIT
setopt ERR_RETURN
setopt NO_UNSET
setopt PIPE_FAIL

# Simple gersemi formatter script
main() {
  local gersemi=""
  
  # Find gersemi
  if command -v gersemi >/dev/null 2>&1; then
    gersemi="gersemi"
  elif [[ -x "${PWD}/.venv/bin/gersemi" ]]; then
    gersemi="${PWD}/.venv/bin/gersemi"
  else
    echo "❌ gersemi not found. Install with: pip install gersemi" >&2
    exit 2
  fi

  # Handle command line arguments
  if [[ "${1:-}" == "--version" ]]; then
    echo "run-gersemi 1.0.0"
    exit 0
  fi
  
  if [[ "${1:-}" == "--check" ]]; then
    shift
    local files=($@)
    if [[ ${#files} -eq 0 ]]; then
      files=(CMakeLists.txt cmake/**/*.cmake(.N) **/*CMakeLists.txt(.N))
    fi
    
    local num_failures=0
    for file in $files; do
      if [[ -f $file ]]; then
        echo "Checking $file" >&2
        if ! $gersemi --check "$file"; then
          echo "❌ File $file is not correctly formatted" >&2
          ((num_failures++))
        fi
      fi
    done
    
    if [[ $num_failures -eq 0 ]]; then
      echo "✅ All CMake files are correctly formatted" >&2
    else
      exit $num_failures
    fi
  else
    # Format files
    local files=($@)
    if [[ ${#files} -eq 0 ]]; then
      files=(CMakeLists.txt cmake/**/*.cmake(.N) **/*CMakeLists.txt(.N))
    fi
    
    for file in $files; do
      if [[ -f $file ]]; then
        echo "Formatting $file" >&2
        $gersemi --in-place "$file"
      fi
    done
    
    echo "✅ Formatted ${#files} CMake files" >&2
  fi
}

main "$@"
