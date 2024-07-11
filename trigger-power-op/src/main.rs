// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

//! This crate provides a command line utility trigger-power-op that lets users
//! trigger various power operations, such as suspend, shutdown, and reboot.
//!
//! Example usage that triggers a suspend command: trigger-power-op suspend
//!
//! The application interacts with systemd-logind through [systemd-logind's DBus interface](https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.login1.html).
//!
//! The crate currently has dependencies on rustbus for DBus communication and argh
//! for command line parsing.
//!
//! ### Note: this crate currently depends on both syn 2.0 and syn 1.0
//!  1. rustbus depends on rustbus_derive, which currently depends on syn 1.0.
//!  2. argh depends on argh_derive, which currently depends on syn 2.0.
//!
//! The rustbus maintainer has already updated rustbus_derive source code to depend on
//! syn 2.0, but has not pushed the update as a new crate release to crates.io.

use std::error::Error;
use std::fmt;
use std::result::Result;
use std::time::Duration;

use argh::FromArgs;
use rustbus::{
    connection::{
        ll_conn::force_finish_on_error,
        Timeout,
    },
    MessageBuilder,
    MessageType,
    RpcConn,
};

/// Used when an incoming DBus message / response has message type ERROR.
/// See [DBus specification Message Format](https://dbus.freedesktop.org/doc/dbus-specification.html#message-protocol-messages) to learn about the ERROR message type
/// and the ERROR_NAME header field.
#[derive(Debug, Clone)]
struct DBusResponseError {
    /// Holds the DBus error message's ERROR_NAME header field.
    name: String,
}

impl DBusResponseError {
    fn new(error_name: &str) -> DBusResponseError {
        DBusResponseError {
            name: error_name.to_string(),
        }
    }
}

impl Error for DBusResponseError {}

/// This displays the DBus ERROR_NAME given by the DBus error message.
impl fmt::Display for DBusResponseError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.name.as_str())
    }
}

/// Default timeout for DBus operations. Ensures that DBus communication times out
/// instead of waiting forever.
static DEFAULT_DBUS_TIMEOUT: Timeout = Timeout::Duration(Duration::new(1, 0));

// logind dbus info
static LOGIND_DEST: &str = "org.freedesktop.login1";
static LOGIND_PATH: &str = "/org/freedesktop/login1";
static LOGIND_INTERFACE: &str = "org.freedesktop.login1.Manager";

/// See SD_LOGIND_ROOT_CHECK_INHIBITORS at [org.freedesktop.login1](https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.login1.html).
static POWER_OP_DBUS_FLAGS: u64 = 0x01;

#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "suspend")]
/// suspend the system
struct SuspendCommand {}

#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "shutdown")]
/// shutdown the system
struct ShutdownCommand {}

#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "reboot")]
/// reboot the system
struct RebootCommand {}

#[derive(FromArgs, Debug)]
#[argh(subcommand)]
enum PowerOpCommand {
    Suspend(SuspendCommand),
    Shutdown(ShutdownCommand),
    Reboot(RebootCommand),
}

impl PowerOpCommand {
    /// Converts the users's subcommand to the appropriate DBus method to send to systemd-logind.
    fn dbus_method(&self) -> String {
        let s = match self {
            PowerOpCommand::Suspend(_) => "SuspendWithFlags",
            PowerOpCommand::Shutdown(_) => "PowerOffWithFlags",
            PowerOpCommand::Reboot(_) => "RebootWithFlags",
        };
        s.to_string()
    }
}

#[derive(FromArgs, Debug)]
/// Trigger power operation
struct TriggerPowerOp {
    #[argh(subcommand)]
    cmd: PowerOpCommand,
}

fn main() -> Result<(), Box<dyn Error>> {
    let power_op: TriggerPowerOp = argh::from_env();
    let dbus_method: String = power_op.cmd.dbus_method();

    let mut rpc_con = RpcConn::system_conn(DEFAULT_DBUS_TIMEOUT)?;

    let mut call = MessageBuilder::new()
        .call(dbus_method)
        .with_interface(LOGIND_INTERFACE)
        .on(LOGIND_PATH)
        .at(LOGIND_DEST)
        .build();

    call.body.push_param(POWER_OP_DBUS_FLAGS)?;

    let id = rpc_con.send_message(&mut call)?.write_all().map_err(force_finish_on_error)?;

    let reply = rpc_con.wait_response(id, DEFAULT_DBUS_TIMEOUT)?;

    if reply.typ == MessageType::Error {
        if let Some(error_name) = reply.dynheader.error_name {
            return Err(Box::new(DBusResponseError::new(&error_name)));
        }
    }

    Ok(())
}
