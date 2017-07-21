# Copyright (C) 2015-2017 Advanced Micro Devices, Inc.
#
# This file is distributed under the BSD license described in tools/COPYING
#
# This Rscript saves big IBS op csv files as smaller Rdata objects.
#
# Usage:
# Rscript op_csv2Rdata.R file1 [file2 ...]
# Read file1.csv [, file2.csv, ...] and output file1.Rdata [, file2.Rdata, ...].

# Use this function to read in data when the CSV is too big for read.csv, but the data isn't.
# Basic idea is to read a 'chunk' of a big csv at a time, and add it to the data frame.
assign.from.chunky.csv <- function(var.name, file, chunk.rows)
{
	assign(var.name, read.csv(file, nrows=chunk.rows), inherits=TRUE)
	tmp.nrows.read <- nrow(get(var.name))
	tmp.skipped.count <- 0
	print(nrow(get(var.name)))	# Print progress
	while (tmp.nrows.read == chunk.rows) {
		tmp.skipped.count <- tmp.skipped.count + 1
		tmp.df <- read.csv(file, skip=(chunk.rows * tmp.skipped.count), nrows=chunk.rows)
		tmp.nrows.read <- nrow(tmp.df)
		colnames(tmp.df) <- colnames(get(var.name))
		assign(var.name, rbind(get(var.name), tmp.df), inherits=TRUE)
		print(nrow(get(var.name)))	# Print progress
		rm(tmp.df)
	}
	rm(tmp.nrows.read)
	rm(tmp.skipped.count)
}

# Read CSV files and save the R data frames
for (name in commandArgs(TRUE)) {
	var.name <- paste('op.', name, sep='')
	print(var.name)
	print(system.time(
		assign.from.chunky.csv(var.name, paste(name, '.csv', sep=''), 2e6)
	))
	save(list=var.name, file=paste(name, '.Rdata', sep=''))
	rm(list=var.name)
	gc()
}
