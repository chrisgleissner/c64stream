# Testing E2E CI Fixes - Quick Guide

## Recommended Testing Sequence

### 1. Manual E2E Test (Fastest Validation)

Test the latest build from main branch:

```
GitHub Actions → Manual E2E Test → Run workflow

Inputs:
  source_type: latest_build
  format: PAL
  frames: 250
```

**Expected Result:**
- ✅ Workflow downloads latest build artifact via nightly.link
- ✅ Plugin installed successfully (detailed logs shown)
- ✅ E2E test runs with OBS
- ✅ Artifacts uploaded including OBS logs

**Time:** ~5-10 minutes

### 2. Check Logs

Review the workflow logs for:

```
📦 Download and Install Plugin
  - Should show artifact type detection
  - Should show extraction and installation steps
  - Should verify plugin installation with paths and checksums

🎬 Run E2E Test  
  - Should show verbose plugin installation details
  - Should show OBS startup
  - Should show packet replay
  - Should show validation results

📤 Upload Test Results
  - Should include OBS logs
  - Should include CSV recordings
  - Should include validation JSON
```

### 3. Review Artifacts

Download the uploaded test results:

```
e2e-test-results-<run_id>/
  - test_output/*.csv    # Plugin CSV recordings
  - test_output/*.json   # Validation results
  - test_output/*.mkv    # Video recording (if successful)
  - logs/*.txt           # OBS Studio logs
```

**Key Files to Check:**
- `network.csv` - Should show received packets
- `obs.csv` - Should show processed frames
- `validation_results.json` - Should show pass/warning/fail status
- OBS logs - Should show plugin loading and initialization

### 4. Automated E2E Test

The automated E2E test will run on the next push to main:

```bash
# Make a trivial change and push to main
git checkout main
git pull
echo "# Test comment" >> README.md
git add README.md
git commit -m "test: Trigger E2E workflow"
git push
```

**Expected Result:**
- ✅ Build completes successfully
- ✅ E2E test runs automatically
- ✅ Uses the .tar.xz artifact from that build
- ✅ Plugin installs and test passes

## Common Issues and Solutions

### Issue: "No .tar.xz file found in artifact"

**Cause:** Using a release artifact instead of a build artifact

**Solution:** 
- For manual testing: Use `source_type: latest_build`
- For automated testing: Verify the build created a .tar.xz file

### Issue: "Plugin binary not found"

**Cause:** Artifact has different internal structure than expected

**Solution:** Check the workflow logs for the "Searching for .so files" output to see actual structure

### Issue: "OBS failed to start"

**Cause:** Xvfb or OBS Studio issues on CI runner

**Solution:** Check OBS logs in artifacts for error messages

### Issue: E2E test timeout

**Cause:** Test hanging or taking too long

**Solution:**
- Reduce frames: Use `frames: 100` for faster testing
- Check resource monitoring logs if enabled
- Review OBS logs for deadlocks

## Verification Checklist

After testing, verify:

- [ ] Manual workflow with `latest_build` completes successfully
- [ ] Logs show correct artifact type detection
- [ ] Logs show detailed plugin installation paths
- [ ] OBS logs are included in artifacts
- [ ] Validation results show expected status
- [ ] Video recording created (if full test passed)
- [ ] CSV files show packet reception and frame processing
- [ ] Automated E2E on main branch works

## Success Criteria

The fixes are considered successful when:

1. ✅ Manual E2E test with `latest_build` completes without errors
2. ✅ Plugin installs correctly from .tar.xz artifact
3. ✅ Detailed logs clearly show installation steps
4. ✅ OBS logs are available for debugging
5. ✅ E2E test validates packet reception and processing
6. ✅ Automated E2E test passes on main branch

## Next Steps if Issues Found

1. **Review logs** in artifacts to identify specific failure
2. **Check plugin installation** paths and checksums
3. **Review OBS logs** for runtime errors
4. **Adjust the workflow** based on actual artifact structure
5. **Iterate** with fixes and retest

## Support

If issues persist:

1. Download the complete test artifacts
2. Review the e2e-ci-fixes.md documentation
3. Check for differences between local and CI environment
4. Consider running E2E locally to compare behavior
