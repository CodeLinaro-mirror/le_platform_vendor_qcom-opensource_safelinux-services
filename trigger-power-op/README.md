# trigger-power-op crate

This is a rust crate that provides a command line utility trigger-power-op that lets
users trigger various power operations, such as suspend, shutdown, and reboot.

## How to build

[First Steps with Cargo](https://doc.rust-lang.org/cargo/getting-started/first-steps.html)

The build steps follow the basic cargo build command:

```cargo build```

## Generate documentation

To build & open documentation for this crate only:

```cargo doc --no-deps --open```

To build & open documentation for this crate and all of its dependencies:

```cargo doc --open```

cargo creates documentation from the documentation comments in the rust source code.

## Update the Cargo.lock file

The Cargo.lock file fixes which versions of crate dependencies to use for this rust
crate. When this rust crate is updated and it requires new functionality provided
by crates that are not currently listed in Cargo.lock, or by new versions of crates
listed in Cargo.lock, the Cargo.lock should be updated.

Run the following command to update the Cargo.lock file:

```cargo generate-lockfile```
