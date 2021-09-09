extern crate clap;
use clap::{crate_version, crate_authors, crate_description, load_yaml};
use clap::App;

fn main() {
    let yaml = load_yaml!("cli.yml");
    let matches = App::from_yaml(yaml)
        .version(crate_version!())
        .about(crate_description!())
        .author(crate_authors!("\n"))
        .get_matches();

    if let Some(user) = matches.value_of("rpcuser") {
        println!("Rpc user : {}", user);
    }
}
