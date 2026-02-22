# Fix line endings (from repo root)
find . -type f \
  \( -name "*.sh" -o -name "*.cpp" -o -name "*.c" -o -name "*.h" \
     -o -name "*.mk" -o -name "Makefile" -o -name "*.txt" \
     -o -name "*.yaml" -o -name "*.yml" -o -name "*.json" \
     -o -name "*.ini" -o -name "*.py" -o -name "*.gradle" \) \
  -not -path "./firmware/ext/*" \
  -not -path "./firmware/ChibiOS/*" \
  -not -path "./unit_tests/googletest/*" \
  -exec dos2unix {} \;

# Fix permissions
find . -name "*.sh" -not -path "./firmware/ext/*" -not -path "./firmware/ChibiOS/*" -exec chmod +x {} \;
find . -name "*.sh" -not -path "./firmware/ext/*" -not -path "./firmware/ChibiOS/*" -exec chmod +x {} \;