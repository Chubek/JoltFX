use clap::{Parser, Subcommand};

#[derive(Parser)]
#[command(name = "zoltan")]
#[command(about = "JoltScript compiler and live preview tool")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Compile a JoltScript file
    Compile {
        #[arg(value_name = "FILE")]
        input: String,
        #[arg(short, long, value_name = "FILE")]
        output: Option<String>,
    },
    /// Start live preview server
    Preview {
        #[arg(short, long, default_value = "7072")]
        port: u16,
    },
}

fn main() {
    let cli = Cli::parse();

    match cli.command {
        Commands::Compile { input, output } => {
            println!("Compiling: {}", input);
            if let Some(out) = output {
                println!("Output: {}", out);
            }
            // TODO: Implement compilation
        }
        Commands::Preview { port } => {
            println!("Starting preview server on port {}", port);
            // TODO: Implement live preview
        }
    }
}
