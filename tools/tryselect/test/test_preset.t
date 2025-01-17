  $ . $TESTDIR/setup.sh
  $ cd $topsrcdir

Test preset with no subcommand

  $ ./mach try $testargs --save foo -b do -p linux -u mochitests -t none --tag foo
  Calculated try selector:
  try: -b do -p linux -u mochitests -t none --tag foo
  Commit message:
  try: -b do -p linux -u mochitests -t none --tag foo
  
  Pushed via `mach try syntax`
  preset saved, run with: --preset=foo
  $ ./mach try $testargs --preset foo
  Calculated try selector:
  try: -b do -p linux -u mochitests -t none --tag foo
  Commit message:
  try: -b do -p linux -u mochitests -t none --tag foo
  
  Pushed via `mach try syntax`
  $ ./mach try syntax $testargs --preset foo
  Calculated try selector:
  try: -b do -p linux -u mochitests -t none --tag foo
  Commit message:
  try: -b do -p linux -u mochitests -t none --tag foo
  
  Pushed via `mach try syntax`
  $ ./mach try $testargs --list-presets
  foo: -b do -p linux -u mochitests -t none --tag foo

Test preset with syntax subcommand

  $ ./mach try syntax $testargs --save bar -b do -p win32 -u none -t all --tag bar
  Calculated try selector:
  try: -b do -p win32 -u none -t all --tag bar
  Commit message:
  try: -b do -p win32 -u none -t all --tag bar
  
  Pushed via `mach try syntax`
  preset saved, run with: --preset=bar
  $ ./mach try syntax $testargs --preset bar
  Calculated try selector:
  try: -b do -p win32 -u none -t all --tag bar
  Commit message:
  try: -b do -p win32 -u none -t all --tag bar
  
  Pushed via `mach try syntax`
  $ ./mach try $testargs --preset bar
  Calculated try selector:
  try: -b do -p win32 -u none -t all --tag bar
  Commit message:
  try: -b do -p win32 -u none -t all --tag bar
  
  Pushed via `mach try syntax`
  $ ./mach try syntax $testargs --list-presets
  foo: -b do -p linux -u mochitests -t none --tag foo
  bar: -b do -p win32 -u none -t all --tag bar

Test preset with fuzzy subcommand

  $ ./mach try fuzzy $testargs --save baz -q "'baz"
  preset saved, run with: --preset=baz
  Calculated try selector:
  {
    "tasks":[
      "build-baz"
    ]
  }
  
  Commit message:
  Fuzzy with query: 'baz
  
  Pushed via `mach try fuzzy`
  $ ./mach try fuzzy $testargs --preset baz
  Calculated try selector:
  {
    "tasks":[
      "build-baz"
    ]
  }
  
  Commit message:
  Fuzzy with query: 'baz
  
  Pushed via `mach try fuzzy`
  $ ./mach try $testargs --preset baz
  Calculated try selector:
  {
    "tasks":[
      "build-baz"
    ]
  }
  
  Commit message:
  Fuzzy with query: 'baz
  
  Pushed via `mach try fuzzy`
  $ ./mach try fuzzy $testargs --list-presets
  baz: 'baz
