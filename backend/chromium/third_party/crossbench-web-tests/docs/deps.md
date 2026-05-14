# How to Update the Crossbench Dependency

This guide provides a step-by-step process for updating the
`third_party/crossbench` dependency.

### Step 1: Roll the Crossbench Dependency

Use the `roll-dep` tool to update the revision of `crossbench` in the `DEPS`
file. Run the following command from the project's root directory:

```bash
roll-dep third_party/crossbench
```

This will create a new commit with the updated dependency.

### Step 2: Update .vpython3 Packages

The `.vpython3` file in the project root needs to be synchronized with the one
in the newly updated `crossbench` dependency. Copy the package versions from
`third_party/crossbench/.vpython3` to the root `.vpython3` file.

### Step 3: Update Poetry Dependencies

The python project at `cuj/crossbench/runner` uses Poetry for dependency
management. Navigate to that directory and update the dependencies:

```bash
cd cuj/crossbench/runner
poetry lock
poetry install
```

This will update the `poetry.lock` file and install the new package versions.
After this, add the changed `poetry.lock` file to your commit and amend it
again.

```bash
git add poetry.lock
git commit --amend
```

### Step 4: Verify the Update

After `roll-dep` completes, run the presubmit checks to ensure that the new
version of `crossbench` doesn't introduce any breaking changes:

```bash
git cl presubmit
```

### Step 5: Fix Breaking Changes (If Necessary)

If the presubmit checks fail, you will need to fix the issues. Once you have
fixed the breaking changes, amend the commit created by `roll-dep` to include
your fixes. This keeps the dependency roll and the necessary fixes in a single,
atomic commit.

```bash
git commit --amend
```
