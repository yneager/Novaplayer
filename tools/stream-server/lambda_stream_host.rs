//! LAMBDA Player's host for the open-source Stremio-compatible streaming
//! server (github.com/stremio-native/stream-server, MIT).
//!
//! The upstream `server` binary binds 0.0.0.0:11470 (+ HTTPS 12470), shows a
//! tray icon, announces itself over SSDP and updates itself. LAMBDA only needs
//! the HTTP API on the loopback interface, so this host starts the library with
//! its `embedded()` configuration instead:
//!
//!   lambda-stream-server --port <n|0> --config-dir <dir> --cache-dir <dir>
//!                        [--log]
//!
//! It prints `LAMBDA_STREAM_SERVER_READY http://127.0.0.1:<port>` once the
//! listener is bound, and shuts down when its standard input reaches EOF, so
//! the server never outlives the player (also when the player crashes).
//!
//! Built as an example of the upstream `server` crate (see build.ps1), which
//! keeps the upstream Cargo.lock in charge of every dependency version.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::io::Read;
use std::net::{Ipv4Addr, SocketAddr};
use std::path::PathBuf;

fn main() {
    if let Err(err) = run() {
        eprintln!("LAMBDA_STREAM_SERVER_ERROR {err:#}");
        std::process::exit(1);
    }
}

fn run() -> anyhow::Result<()> {
    let mut port: u16 = 0;
    let mut config_dir: Option<PathBuf> = None;
    let mut cache_dir: Option<PathBuf> = None;
    let mut log = false;

    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        let mut value = || {
            args.next()
                .ok_or_else(|| anyhow::anyhow!("missing value for {arg}"))
        };
        match arg.as_str() {
            "--port" => port = value()?.parse()?,
            "--config-dir" => config_dir = Some(PathBuf::from(value()?)),
            "--cache-dir" => cache_dir = Some(PathBuf::from(value()?)),
            "--log" => log = true,
            other => anyhow::bail!("unknown argument {other}"),
        }
    }

    let handle = stream_server::start(stream_server::ServerConfig {
        http_addr: SocketAddr::from((Ipv4Addr::LOCALHOST, port)),
        config_dir,
        cache_dir,
        init_logging: log,
        ..stream_server::ServerConfig::embedded()
    })?;

    println!(
        "LAMBDA_STREAM_SERVER_READY http://{}",
        handle.http_addr()
    );

    // Block until the parent closes our stdin (or exits).
    let mut sink = [0u8; 256];
    let mut stdin = std::io::stdin();
    while matches!(stdin.read(&mut sink), Ok(n) if n > 0) {}

    handle.shutdown()?;
    handle.join()?;
    Ok(())
}
