/**
*    Copyright (C) 2008 10gen Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "mongo/pch.h"

#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <pcrecpp.h>

#include "mongo/client/dbclientcursor.h"
#include "mongo/client/gridfs.h"
#include "mongo/tools/tool.h"

using namespace mongo;

namespace po = boost::program_options;

class Files : public Tool {
public:
    Files() : Tool( "files" ) {
        // Default collection for GridFS
        _coll = "fs";

        add_options()
        ( "local,l", po::value<string>(), "local filename for put|get (default is to use the same name as 'gridfs filename')")
        ( "type,t", po::value<string>(), "MIME type for put (default is to omit)")
        ( "replace,r", "Remove other files with same name after PUT")
        ( "chunk-size,s", po::value<int>(), "Chunk size for storing files (bytes)")
        ;
        add_hidden_options()
        ( "command" , po::value<string>() , "command (list|search|put|get)" )
        ( "file" , po::value<string>() , "filename for get|put" )
        ;
        addPositionArg( "command" , 1 );
        addPositionArg( "file" , 2 );
    }

    virtual void printExtraHelp( ostream & out ) {
        out << "Browse and modify a GridFS filesystem.\n" << endl;
        out << "usage: " << _name << " [options] command [gridfs filename]" << endl;
        out << "command:" << endl;
        out << "  one of (list|search|put|get)" << endl;
        out << "  list - list all files.  'gridfs filename' is an optional prefix " << endl;
        out << "         which listed filenames must begin with." << endl;
        out << "  search - search all files. 'gridfs filename' is a substring " << endl;
        out << "           which listed filenames must contain." << endl;
        out << "  put - add a file with filename 'gridfs filename'" << endl;
        out << "  get - get a file with filename 'gridfs filename'" << endl;
        out << "  delete - delete all files with filename 'gridfs filename'" << endl;
    }

    void display( GridFS * grid , BSONObj obj ) {
        auto_ptr<DBClientCursor> c = grid->list( obj );
        while ( c->more() ) {
            BSONObj obj = c->next();
            cout
                    << obj["filename"].str() << "\t"
                    << (long)obj["length"].number()
                    << endl;
        }
    }

    int run() {
        string cmd = getParam( "command" );
        if ( cmd.size() == 0 ) {
            cerr << "ERROR: need command" << endl << endl;
            printHelp(cout);
            return -1;
        }

        GridFS g( conn() , _db, _coll );

        string filename = getParam( "file" );

        if ( cmd == "list" ) {
            BSONObjBuilder b;
            if ( filename.size() ) {
                b.appendRegex( "filename" , (string)"^" +
                               pcrecpp::RE::QuoteMeta( filename ) );
            }
            
            display( &g , b.obj() );
            return 0;
        }

        if ( filename.size() == 0 ) {
            cerr << "ERROR: need a filename" << endl << endl;
            printHelp(cout);
            return -1;
        }

        if ( cmd == "search" ) {
            BSONObjBuilder b;
            b.appendRegex( "filename" , filename );
            display( &g , b.obj() );
            return 0;
        }

        if ( cmd == "get" ) {
            GridFile f = g.findFile( filename );
            if ( ! f.exists() ) {
                cerr << "ERROR: file not found" << endl;
                return -2;
            }

            string out = getParam("local", f.getFilename());
            f.write( out );

            if (out != "-")
                cout << "done write to: " << out << endl;

            return 0;
        }

        if ( cmd == "put" ) {
            const string& infile = getParam("local", filename);
            const string& type = getParam("type", "");

            if (hasParam("chunk-size")) {
                int chunk_size = getParam("chunk-size", 0);
                if (chunk_size < 0) {
                    cerr << "ERROR: Chunk size cannot be negative" << endl;
                    return -3;
                } else if (chunk_size > (BSONObjMaxUserSize - (16 * 1024))) {
                    cerr << "ERROR: Chunk size beyond maximum document size" << endl;
                    return -3;
                } else if (chunk_size > 0) {
                    g.setChunkSize(chunk_size);
                }
            }

            BSONObj file = g.storeFile(infile, filename, type);
            cout << "added file: " << file << endl;

            if (hasParam("replace")){
                auto_ptr<DBClientCursor> cursor =
                  conn().query(_db + "." _coll + ".files",
                               BSON("filename" << filename << "_id" << NE << file["_id"] ));
                while (cursor->more()){
                    BSONObj o = cursor->nextSafe();
                    conn().remove(_db + "." + _coll + ".files", BSON("_id" << o["_id"]));
                    conn().remove(_db + "." + _coll + ".chunks", BSON("files_id" << o["_id"]));
                    cout << "removed file: " << o << endl;
                }

            }

            conn().getLastError();
            cout << "done!" << endl;
            return 0;
        }

        if ( cmd == "delete" ) {
            g.removeFile(filename);
            conn().getLastError();
            cout << "done!" << endl;
            return 0;
        }

        cerr << "ERROR: unknown command '" << cmd << "'" << endl << endl;
        printHelp(cout);
        return -1;
    }
};

REGISTER_MONGO_TOOL(Files);
