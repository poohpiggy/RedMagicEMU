package app.aemu.importer

import java.io.IOException

/** The user requested cancellation; other I/O and cleanup failures remain errors. */
internal class ImportCancelledException : IOException("cancelled")
