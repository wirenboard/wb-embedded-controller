// Coverage is collected in the regular package build: cov-report=../coverage makes
// debian/rules run "make coverage", which writes coverage.xml to the sbuild build dir,
// where buildDebSbuild picks it up (RUN_COVERAGE) and sends it to coveralls.
// "noautodbgsym parallel=8" are the buildDebSbuild defaults and must be kept.
buildDebSbuild defaultRunCoverage: true,
               defaultCoverageMin: '27',
               defaultDoCoverallsReporting: true,
               defaultDebBuildOptions: 'noautodbgsym parallel=8 cov-report=../coverage'
