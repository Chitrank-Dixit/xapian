/* enquire.cc: External interface for running queries
 *
 * ----START-LICENCE----
 * Copyright 1999,2000 Dialog Corporation
 * 
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 * -----END-LICENCE-----
 */

#include "omassert.h"
#include "utils.h"
#include "omlocks.h"
#include "omqueryinternal.h"

#include <om/omerror.h>
#include <om/omenquire.h>

#include "rset.h"
//#include "match.h"
#include "leafmatch.h"
#include "expand.h"
#include "database.h"
#include "database_builder.h"
#include <om/omdocument.h>

#include <vector>
#include <set>
#include <memory>
#include <algorithm>
#include <math.h>

// Table of names of database types
stringToType<om_database_type> stringToTypeMap<om_database_type>::types[] = {
    { "da_flimsy",		OM_DBTYPE_MUSCAT36_DA_F	},
    { "da_heavy",		OM_DBTYPE_MUSCAT36_DA_H	},
    { "db_flimsy",		OM_DBTYPE_MUSCAT36_DB_F	},
    { "db_heavy",		OM_DBTYPE_MUSCAT36_DB_H	},
    { "inmemory",		OM_DBTYPE_INMEMORY	},
    { "multidb",		OM_DBTYPE_MULTI		},
    { "sleepycat",		OM_DBTYPE_SLEEPY	},
    { "",			OM_DBTYPE_NULL		}  // End
};

/////////////////////////
// Methods for OmQuery //
/////////////////////////

OmQuery::OmQuery(const om_termname & tname_,
		 om_termcount wqf_,
		 om_termpos term_pos_)
	: internal(0)
{
    internal = new OmQueryInternal(tname_, wqf_, term_pos_);
}

OmQuery::OmQuery(om_queryop op_, const OmQuery &left, const OmQuery &right)
	: internal(0)
{
    internal = new OmQueryInternal(op_,
				   *(left.internal),
				   *(right.internal));
}

OmQuery::OmQuery(om_queryop op_,
		 const vector<OmQuery *>::const_iterator qbegin,
		 const vector<OmQuery *>::const_iterator qend)
	: internal(0)
{
    vector<OmQueryInternal *> temp;
    vector<OmQuery *>::const_iterator i = qbegin;
    while (i != qend) {
	temp.push_back((*i)->internal);
	++i;
    }
    internal = new OmQueryInternal(op_, temp.begin(), temp.end());
}

OmQuery::OmQuery(om_queryop op_,
		 const vector<OmQuery>::const_iterator qbegin,
		 const vector<OmQuery>::const_iterator qend)
	: internal(0)
{   
    vector<OmQueryInternal *> temp;
    vector<OmQuery>::const_iterator i = qbegin;
    while (i != qend) {
	temp.push_back(i->internal);
	++i;
    }
    internal = new OmQueryInternal(op_, temp.begin(), temp.end());
}


OmQuery::OmQuery(om_queryop op_,
		 const vector<om_termname>::const_iterator tbegin,
		 const vector<om_termname>::const_iterator tend)
	: internal(0)
{
    internal = new OmQueryInternal(op_, tbegin, tend);
}

// Copy constructor
OmQuery::OmQuery(const OmQuery & copyme)
	: internal(0)
{
    internal = new OmQueryInternal(*(copyme.internal));
}

// Assignment
OmQuery &
OmQuery::operator=(const OmQuery & copyme)
{
    OmQueryInternal * temp = new OmQueryInternal(*(copyme.internal));
    swap(temp, this->internal);
    delete temp;

    return *this;
}

// Default constructor
OmQuery::OmQuery()
	: internal(0)
{
    internal = new OmQueryInternal();
}

// Destructor
OmQuery::~OmQuery()
{
    delete internal;
}

string OmQuery::get_description() const
{
    OmLockSentry locksentry(internal->mutex);
    return internal->get_description();
}

bool OmQuery::is_defined() const
{
    OmLockSentry locksentry(internal->mutex);
    return internal->is_defined();
}

bool OmQuery::is_bool() const
{
    OmLockSentry locksentry(internal->mutex);
    return internal->is_bool();
}

bool OmQuery::set_bool(bool isbool_)
{
    OmLockSentry locksentry(internal->mutex);
    return internal->set_bool(isbool_);
}

om_termcount OmQuery::get_length() const
{
    OmLockSentry locksentry(internal->mutex);
    return internal->get_length();
}

om_termcount OmQuery::set_length(om_termcount qlen_)
{
    OmLockSentry locksentry(internal->mutex);
    return internal->set_length(qlen_);
}

om_termname_list OmQuery::get_terms() const
{
    OmLockSentry locksentry(internal->mutex);
    return internal->get_terms();
}

/////////////////////////////////
// Methods for OmQueryInternal //
/////////////////////////////////

// Introspection
string
OmQueryInternal::get_description() const
{
    if(!isdefined) return "<NULL>";
    string opstr;
    switch(op) {
	case OM_MOP_LEAF:
		return tname;
		break;
	case OM_MOP_AND:
		opstr = " AND ";
		break;
	case OM_MOP_OR:
		opstr = " OR ";
		break;
	case OM_MOP_FILTER:
		opstr = " FILTER ";
		break;
	case OM_MOP_AND_MAYBE:
		opstr = " AND_MAYBE ";
		break;
	case OM_MOP_AND_NOT:
		opstr = " AND_NOT ";
		break;
	case OM_MOP_XOR:
		opstr = " XOR ";
		break;
    }
    string description;
    vector<OmQueryInternal *>::const_iterator i;
    for(i = subqs.begin(); i != subqs.end(); i++) {
	if(description.size()) description += opstr;
	description += (**i).get_description();
    }
    return "(" + description + ")";
}

bool
OmQueryInternal::set_bool(bool isbool_)
{
    bool oldbool = isbool;
    isbool = isbool_;
    return oldbool;
}

om_termcount
OmQueryInternal::set_length(om_termcount qlen_)
{
    om_termcount oldqlen = qlen;
    qlen = qlen_;
    return oldqlen;
}

void
OmQueryInternal::accumulate_terms(
			vector<pair<om_termname, om_termpos> > &terms) const
{ 
    Assert(isdefined);

    if (op == OM_MOP_LEAF) {
        // We're a leaf, so just return our term.
        terms.push_back(make_pair(tname, term_pos));
    } else {
    	subquery_list::const_iterator end = subqs.end();
        // not a leaf, concatenate results from all subqueries
	for (subquery_list::const_iterator i = subqs.begin();
	     i != end;
	     ++i) {
 	    (*i)->accumulate_terms(terms);
	}
    }
}

struct LessByTermpos {
    typedef const pair<om_termname, om_termpos> argtype;
    bool operator()(argtype &left, argtype &right) {
	if (left.second != right.second) {
	    return left.second < right.second;
	} else {
	    return left.first < right.first;
	}
    }
};

om_termname_list
OmQueryInternal::get_terms() const
{
    om_termname_list result;

    vector<pair<om_termname, om_termpos> > terms;
    if (isdefined) {
        accumulate_terms(terms);
    }

    sort(terms.begin(), terms.end(), LessByTermpos());

    // remove adjacent duplicates, and return an iterator pointing
    // to just after the last unique element
    vector<pair<om_termname, om_termpos> >::iterator newlast =
	    	unique(terms.begin(), terms.end());
    // and remove the rest...  (See Stroustrup 18.6.3)
    terms.erase(newlast, terms.end());

    vector<pair<om_termname, om_termpos> >::const_iterator i;
    for (i=terms.begin(); i!= terms.end(); ++i) {
	result.push_back(i->first);
    }

    return result;
}

OmQueryInternal::OmQueryInternal()
	: mutex(), isdefined(false), qlen(0)
{}

OmQueryInternal::OmQueryInternal(const OmQueryInternal &copyme)
	: mutex(), isdefined(copyme.isdefined),
	isbool(copyme.isbool), op(copyme.op),
	subqs(subquery_list()), qlen(copyme.qlen),
	tname(copyme.tname), term_pos(copyme.term_pos),
	wqf(copyme.wqf)
{
    // FIXME: not exception safe
    for (subquery_list::const_iterator i = copyme.subqs.begin();
	 i != copyme.subqs.end();
	 ++i) {
	subqs.push_back(new OmQueryInternal(**i));
    }
}

OmQueryInternal::OmQueryInternal(const om_termname & tname_,
		 om_termcount wqf_,
		 om_termpos term_pos_)
	: isdefined(true), isbool(false), op(OM_MOP_LEAF),
	qlen(wqf_), tname(tname_), term_pos(term_pos_), wqf(wqf_)
{}

OmQueryInternal::OmQueryInternal(om_queryop op_,
				 const OmQueryInternal &left,
				 const OmQueryInternal &right)
	: isdefined(true), isbool(false), op(op_),
	  qlen(left.qlen + right.qlen)
{
    if (op == OM_MOP_LEAF) {
    	throw OmInvalidArgumentError("Invalid query operation");
    }

    // reject any attempt to make up a composite query when any sub-query
    // is a pure boolean query.  FIXME: ought to handle the different
    // operators specially.
    if ((left.isdefined && left.isbool) || (right.isdefined && right.isbool)) {
	throw OmInvalidArgumentError("Only the top-level query can be bool");
    }

    // Handle undefined sub-queries.
    // See documentation for table for result of operations when one of the
    // operands is undefined:
    //
    if(!left.isdefined || !right.isdefined) {
	switch (op) {
	    case OM_MOP_OR:
	    case OM_MOP_AND:
		if (left.isdefined) {
		    initialise_from_copy(left);
		} else {
		    if (right.isdefined) initialise_from_copy(right);
		    else isdefined = false;
		}
		break;
	    case OM_MOP_FILTER:
		if (left.isdefined) {
		    initialise_from_copy(left);
		} else {
		    if (right.isdefined) {
			// Pure boolean
			initialise_from_copy(right);
			isbool = true;
		    } else {
			isdefined = false;
		    }
		}
		break;
	    case OM_MOP_AND_MAYBE:
		if (left.isdefined) {
		    initialise_from_copy(left);
		} else {
		    isdefined = false;
		}
		break;
	    case OM_MOP_AND_NOT:
		if (left.isdefined) {
		    initialise_from_copy(left);
		} else {
		    if (right.isdefined) {
		        throw OmInvalidArgumentError("AND NOT can't have an undefined LHS");
		    } else isdefined = false;
		}
		break;
	    case OM_MOP_XOR:
		if (!left.isdefined && !right.isdefined) {
		    isdefined = true;
		} else {
		    throw OmInvalidArgumentError("XOR can't have one undefined argument");
		}
		break;
	    case OM_MOP_LEAF:
		Assert(false); // Shouldn't have got this far
	}
    } else {
	DebugMsg(" (" << left.get_description() << " " << (int) op <<
		 " " << right.get_description() << ") => ");

	// If sub query has same op, which is OR or AND, add to list rather
	// than makeing sub-node.  Can then optimise the list at search time.
	if(op == OM_MOP_AND || op == OM_MOP_OR) {
	    if(left.op == op && right.op == op) {
		// Both queries have same operation as top
		initialise_from_copy(left);
		vector<OmQueryInternal *>::const_iterator i;
		for(i = right.subqs.begin(); i != right.subqs.end(); i++) {
		    subqs.push_back(new OmQueryInternal(**i));
		}
	    } else if(left.op == op) {
		// Query2 has different operation (or is a leaf)
		initialise_from_copy(left);
		subqs.push_back(new OmQueryInternal(right));
		// the initialise_from_copy will have set our
		// qlen to be just left's
		qlen += right.qlen;
	    } else if(right.op == op) { // left has different operation
		// Query1 has different operation (or is a leaf)
		initialise_from_copy(right);
		subqs.push_back(new OmQueryInternal(left));
		// the initialise_from_copy will have set our
		// qlen to be just right's
		qlen += left.qlen;
	    } else {
		subqs.push_back(new OmQueryInternal(left));
		subqs.push_back(new OmQueryInternal(right));
	    }
	} else {
	    subqs.push_back(new OmQueryInternal(left));
	    subqs.push_back(new OmQueryInternal(right));
	}
	collapse_subqs();
	DebugMsg(get_description() << endl);
    }
}

OmQueryInternal::OmQueryInternal(om_queryop op_,
		 const vector<OmQueryInternal *>::const_iterator qbegin,
		 const vector<OmQueryInternal *>::const_iterator qend)
	: isdefined(true), isbool(false), op(op_)
{   
    initialise_from_vector(qbegin, qend);
    collapse_subqs();
}

OmQueryInternal::OmQueryInternal(om_queryop op_,
		 const vector<om_termname>::const_iterator tbegin,
		 const vector<om_termname>::const_iterator tend)
	: isdefined(true), isbool(false), op(op_)
{
    vector<OmQueryInternal *> subqueries;
    vector<om_termname>::const_iterator i;
    for(i = tbegin; i != tend; i++) {
	subqueries.push_back(new OmQueryInternal(*i));
    }
    initialise_from_vector(subqueries.begin(), subqueries.end());
    collapse_subqs();
}

OmQueryInternal::~OmQueryInternal()
{
    vector<OmQueryInternal *>::const_iterator i;
    for(i = subqs.begin(); i != subqs.end(); i++) {
	delete *i;
    }
    subqs.clear();
}

// Copy an OmQueryInternal object into self
void
OmQueryInternal::initialise_from_copy(const OmQueryInternal &copyme)
{
    isdefined = copyme.isdefined;
    isbool = copyme.isbool;
    op = copyme.op;
    qlen = copyme.qlen;
    if(op == OM_MOP_LEAF) {
	tname = copyme.tname;
	term_pos = copyme.term_pos;
	wqf = copyme.wqf;
    } else {
	vector<OmQueryInternal *>::const_iterator i;
	for(i = subqs.begin(); i != subqs.end(); i++) {
	    delete *i;
	}
	subqs.clear();
	for(i = copyme.subqs.begin(); i != copyme.subqs.end(); i++) {
	    subqs.push_back(new OmQueryInternal(**i));
	}
    }
}

// FIXME: this function generated by cut and paste of previous: use a template?
void
OmQueryInternal::initialise_from_vector(
			const vector<OmQueryInternal *>::const_iterator qbegin,
			const vector<OmQueryInternal *>::const_iterator qend)
{
    if ((op != OM_MOP_AND) && (op != OM_MOP_OR)) {
    	throw OmInvalidArgumentError("Vector query op must be AND or OR");
    }
    qlen = 0;

    vector<OmQueryInternal *>::const_iterator i;
    // reject any attempt to make up a composite query when any sub-query
    // is a pure boolean query.  FIXME: ought to handle the different
    // operators specially.
    for (i=qbegin; i!= qend; ++i) {
	if ((*i)->isbool) {
	    throw OmInvalidArgumentError("Only the top-level query can be bool");
	}
    }
    
    for(i = qbegin; i != qend; i++) {
	// FIXME: see other initialise_from_vector comment re exceptions.
	if((*i)->isdefined) {
	    subqs.push_back(new OmQueryInternal(**i));
	    qlen += (*i)->qlen;
	}
    }

    if(subqs.size() == 0) {
	isdefined = false;
    } else if(subqs.size() == 1) {
	// Should just have copied into self
	OmQueryInternal * copyme = subqs[0];
	subqs.clear();
	initialise_from_copy(*copyme);
	delete copyme;
    }
}

struct Collapse_PosNameLess {
    bool operator()(const pair<om_termpos, om_termname> &left,
		    const pair<om_termpos, om_termname> &right) {
	if (left.first != right.first) {
	    return left.first < right.first;
	} else {
	    return left.second < right.second;
	}
    }
};

void OmQueryInternal::collapse_subqs()
{
    // For the moment, anyway, the operation must be OR or AND.
    if ((op == OM_MOP_OR) || (op == OM_MOP_AND)) {
	typedef map<pair<om_termpos, om_termname>,
	            OmQueryInternal *,
		    Collapse_PosNameLess> subqtable;
	subqtable sqtab;
	subquery_list::iterator sq = subqs.begin();
	while (sq != subqs.end()) {
	    if ((*sq)->op == OM_MOP_LEAF) {
		subqtable::key_type key(make_pair((*sq)->term_pos,
						  (*sq)->tname));
		subqtable::iterator s = sqtab.find(key);
		if (s == sqtab.end()) {
		    sqtab[key] = *sq;
		    ++sq;
		} else {
		    s->second->wqf += (*sq)->wqf;
		    // rather than incrementing, delete the current
		    // element, as it has been merged into the other
		    // equivalent term.
		    delete (*sq);
	 	    sq = subqs.erase(sq);
		}
	    } else {
		++sq;
	    }
	}

	// a lone subquery should never disappear...
	Assert(subqs.size() > 0);

	// ...however, we might end up with just one, which
	// we gobble up.
	if (subqs.size() == 1) {
	    OmQueryInternal *only_child = *subqs.begin();
	    subqs.clear();
	    initialise_from_copy(*only_child);
	    delete only_child;
	}
    }
}

////////////////////////////////
// Methods for OmMatchOptions //
////////////////////////////////

OmMatchOptions::OmMatchOptions()
	: do_collapse(false),
	  sort_forward(true),
	  percent_cutoff(-1)
{}

void
OmMatchOptions::set_collapse_key(om_keyno key_)
{
    do_collapse = true;
    collapse_key = key_;
}

void
OmMatchOptions::set_no_collapse()
{
    do_collapse = false;
}

void
OmMatchOptions::set_sort_forward(bool forward_)
{
    sort_forward = forward_;
}

void OmMatchOptions::set_percentage_cutoff(int percent_)
{
    if (percent_ >=0 && percent_ <= 100) {
        percent_cutoff = percent_;
    } else {
        throw OmInvalidArgumentError("Percent cutoff must be in 0..100");
    }
}


/////////////////////////////////
// Methods for OmExpandOptions //
/////////////////////////////////

OmExpandOptions::OmExpandOptions()
	: allow_query_terms(false)
{}

void
OmExpandOptions::use_query_terms(bool allow_query_terms_)
{
    allow_query_terms = allow_query_terms_;
}

OmExpandDeciderFilterTerms::OmExpandDeciderFilterTerms(
                               const om_termname_list &terms)
	: tset(terms.begin(), terms.end()) {}

int
OmExpandDeciderFilterTerms::operator()(const om_termname &tname) const
{
    return (tset.find(tname) == tset.end());
}

OmExpandDeciderAnd::OmExpandDeciderAnd(const OmExpandDecider *left_,
                                       const OmExpandDecider *right_)
        : left(left_), right(right_) {}

int
OmExpandDeciderAnd::operator()(const om_termname &tname) const
{
    return ((*left)(tname)) && ((*right)(tname));
}

////////////////////////
// Methods for OmMSet //
////////////////////////

int
OmMSet::convert_to_percent(om_weight wt) const
{
    int pcent = (int) ceil(wt * 100 / max_possible);
    if(pcent > 100) pcent = 100;
    if(pcent < 0) pcent = 0;
    if(pcent == 0 && wt > 0) pcent = 1;

    return pcent;
}

int
OmMSet::convert_to_percent(const OmMSetItem & item) const
{
    return OmMSet::convert_to_percent(item.wt);
}

/////////////////////////////////
// Internals of OmDatabase     //
/////////////////////////////////
class OmDatabase::Internal {
    public:
	vector<DatabaseBuilderParams> params;

	//void add_database(const DatabaseBuilderParams & newdb);
	void add_database(const string & type,
			  const vector<string> & param);
};

void
OmDatabase::Internal::add_database(const string & type,
			const vector<string> & param)
{
    // Convert type into an om_database_type
    om_database_type dbtype = OM_DBTYPE_NULL;
    dbtype = stringToTypeMap<om_database_type>::get_type(type);

    // Prepare dbparams to build database with (open it readonly)
    DatabaseBuilderParams dbparam(dbtype, true);
    dbparam.paths = param;

    // Use dbparams to create database, and add it to the list of databases
    params.push_back(dbparam);
    //add_database(params);
}

///////////////////////////
// Methods of OmDatabase //
///////////////////////////

OmDatabase::OmDatabase() {
    internal = new OmDatabase::Internal();
}

OmDatabase::~OmDatabase() {
    delete internal;
}

OmDatabase::OmDatabase(const OmDatabase &other)
	: internal(new Internal(*other.internal))
{
}

void OmDatabase::operator=(const OmDatabase &other)
{
    Internal *newinternal = new Internal(*other.internal);

    swap(internal, newinternal);

    delete newinternal;
}

void OmDatabase::add_database(const string &type,
			      const vector<string> &params)
{
    internal->add_database(type, params);
}

/////////////////////////////////
// Internals of enquire system //
/////////////////////////////////

class OmEnquireInternal {
	mutable IRDatabase * database;
	OmDatabase dbdesc;
	
	/* This may need to be mutable in future so that it can be
	 * replaced by an optimised version.
	 */
	OmQuery * query;

    public:
	// pthread mutexes, if available.
	OmLock mutex;

	OmEnquireInternal(const OmDatabase &db);
	~OmEnquireInternal();

	void open_database() const;
	void set_query(const OmQuery & query_);
	OmMSet get_mset(om_doccount first,
			om_doccount maxitems,
			const OmRSet *omrset,
			const OmMatchOptions *moptions,
			const OmMatchDecider *mdecider) const;
	OmESet get_eset(om_termcount maxitems,
			const OmRSet & omrset,
			const OmExpandOptions * eoptions,
			const OmExpandDecider * edecider) const;
	const OmDocument *get_doc(const OmMSetItem &mitem) const;
	const OmDocument *get_doc(om_docid did) const;
	om_termname_list get_matching_terms(const OmMSetItem &mitem) const;
	om_termname_list get_matching_terms(om_docid did) const;
};

//////////////////////////////////////////
// Inline methods for OmEnquireInternal //
//////////////////////////////////////////

inline
OmEnquireInternal::OmEnquireInternal(const OmDatabase &db)
	: database(0), dbdesc(db), query(0)
{
}

inline
OmEnquireInternal::~OmEnquireInternal()
{
    if(database != 0) {
	delete database;
	database = 0;
    }
    if(query != 0) {
	delete query;
	query = 0;
    }
}

// Open the database(s), if not already open.
inline void
OmEnquireInternal::open_database() const
{
    if(database == 0) {
	if(dbdesc.internal->params.size() == 0) {
	    throw OmInvalidArgumentError("Must specify at least one database");
	} else if(dbdesc.internal->params.size() == 1) {
	    database = DatabaseBuilder::create(dbdesc.internal->params.front());
	} else {
	    DatabaseBuilderParams multiparams(OM_DBTYPE_MULTI);
	    multiparams.subdbs = dbdesc.internal->params;
	    database = DatabaseBuilder::create(multiparams);
	}
    }
}

inline void
OmEnquireInternal::set_query(const OmQuery &query_)
{
    if(query) {
	delete query;
	query = 0;
    }
    if (!query_.is_defined()) {
        throw OmInvalidArgumentError("Query must not be undefined");
    }
    query = new OmQuery(query_);
}

OmMSet
OmEnquireInternal::get_mset(om_doccount first,
                    om_doccount maxitems,
                    const OmRSet *omrset,
                    const OmMatchOptions *moptions,
		    const OmMatchDecider *mdecider) const
{
    if(query == 0) {
        throw OmInvalidArgumentError("You must set a query before calling OmEnquire::get_mset()");
    }
    Assert(query->is_defined());

    open_database();
    Assert(database != 0);

    // Use default options if none supplied
    OmMatchOptions defmoptions;
    if (moptions == 0) {
        moptions = &defmoptions;
    }

    // Set Database
    LeafMatch match(database);

    // Set cutoff percent
    if (moptions->percent_cutoff > 0) {
        match.set_min_weight_percent(moptions->percent_cutoff);
    }

    // Set Rset
    RSet *rset = 0;
    if((omrset != 0) && (omrset->items.size() != 0)) {
	rset = new RSet(database, *omrset);
	match.set_rset(rset);
    }

    // Set options
    if(moptions->do_collapse) {
	match.set_collapse_key(moptions->collapse_key);
    }

    // Set Query
    match.set_query(query->internal);

    OmMSet retval;

    // Run query and get results into supplied OmMSet object
    if(query->is_bool()) {
	match.boolmatch(first, maxitems, retval.items);
	retval.mbound = retval.items.size();
	retval.max_possible = 1;
	if(retval.items.size() > 0) {
	    retval.max_attained = 1;
	} else {
	    retval.max_attained = 0;
	}
    } else {
	match.match(first, maxitems, retval.items,
		    msetcmp_forward, &(retval.mbound), &(retval.max_attained),
		    mdecider);

	// Get max weight for an item in the MSet
	retval.max_possible = match.get_max_weight();
    }

    // Store what the first item requested was, so that this information is
    // kept with the mset.
    retval.firstitem = first;

    // Clear up
    delete rset;

    return retval;
}

OmESet
OmEnquireInternal::get_eset(om_termcount maxitems,
                    const OmRSet & omrset,
	            const OmExpandOptions * eoptions,
		    const OmExpandDecider * edecider) const
{
    OmESet retval;

    OmExpandOptions defeoptions;
    if (eoptions == 0) {
        eoptions = &defeoptions;
    }

    open_database();
    OmExpand expand(database);
    RSet rset(database, omrset);

    DebugMsg("rset size is " << rset.get_rsize() << endl);

    OmExpandDeciderAlways decider_always;
    if (edecider == 0) edecider = &decider_always;

    /* The auto_ptrs will clean up any dynamically allocated
     * expand deciders automatically.
     */
    auto_ptr<OmExpandDecider> decider_noquery;
    auto_ptr<OmExpandDecider> decider_andnoquery;
    
    if (query != 0 && !eoptions->allow_query_terms) {
	auto_ptr<OmExpandDecider> temp1(
	    new OmExpandDeciderFilterTerms(query->get_terms()));
        decider_noquery = temp1;
	
	auto_ptr<OmExpandDecider> temp2(
	    new OmExpandDeciderAnd(decider_noquery.get(),
				   edecider));
	decider_andnoquery = temp2;

        edecider = decider_andnoquery.get();
    }
    
    expand.expand(maxitems, retval, &rset, edecider);

    return retval;
}

const OmDocument *
OmEnquireInternal::get_doc(om_docid did) const
{
    open_database();
    OmDocument *doc = database->open_document(did);

    return doc;
}

const OmDocument *
OmEnquireInternal::get_doc(const OmMSetItem &mitem) const
{
    open_database();
    return get_doc(mitem.did);
}

om_termname_list
OmEnquireInternal::get_matching_terms(const OmMSetItem &mitem) const
{
    // FIXME: take advantage of OmMSetItem to ensure that database
    // doesn't change underneath us.
    return get_matching_terms(mitem.did);
}

struct ByQueryIndexCmp {
    typedef map<om_termname, unsigned int> tmap_t;
    const tmap_t &tmap;
    ByQueryIndexCmp(const tmap_t &tmap_) : tmap(tmap_) {};
    bool operator()(const om_termname &left,
		    const om_termname &right) const {
	tmap_t::const_iterator l, r;
	l = tmap.find(left);
	r = tmap.find(right);
	Assert((l != tmap.end()) && (r != tmap.end()));

	return l->second < r->second;
    }
};

om_termname_list
OmEnquireInternal::get_matching_terms(om_docid did) const
{
    if (query == 0) {
        throw OmInvalidArgumentError("Can't get matching terms before setting query");
    }
    Assert(query->is_defined());

    open_database();  // will throw if database not set.

    // the ordered list of terms in the query.
    om_termname_list query_terms = query->get_terms();

    // copy the list of query terms into a map for faster access.
    // FIXME: a hash would be faster than a map, if this becomes
    // a problem.
    map<om_termname, unsigned int> tmap;
    unsigned int index = 1;
    for (om_termname_list::const_iterator i = query_terms.begin();
	 i != query_terms.end();
	 ++i) {
	tmap[*i] = index++;
    }
    
    auto_ptr<TermList> docterms(database->open_term_list(did));
    
    /* next() must be called on a TermList before you can
     * do anything else with it.
     */
    docterms->next();

    vector<om_termname> matching_terms;

    while (!docterms->at_end()) {
        map<om_termname, unsigned int>::iterator t =
		tmap.find(docterms->get_termname());
        if (t != tmap.end()) {
	    matching_terms.push_back(docterms->get_termname());
	}
	docterms->next();
    }
    
    // sort the resulting list by query position.
    sort(matching_terms.begin(),
	 matching_terms.end(),
	 ByQueryIndexCmp(tmap));

    return om_termname_list(matching_terms.begin(), matching_terms.end());
}

////////////////////////////////////////////
// Initialise and delete OmEnquire object //
////////////////////////////////////////////

OmEnquire::OmEnquire(const OmDatabase &db)
{
    internal = new OmEnquireInternal(db);
}

OmEnquire::~OmEnquire()
{
    delete internal;
    internal = NULL;
}

//////////////////
// Set database //
//////////////////

void
OmEnquire::set_query(const OmQuery & query_)
{
    OmLockSentry locksentry(internal->mutex);
    internal->set_query(query_);
}

OmMSet
OmEnquire::get_mset(om_doccount first,
                    om_doccount maxitems,
                    const OmRSet *omrset,
                    const OmMatchOptions *moptions,
		    const OmMatchDecider *mdecider) const
{
    OmLockSentry locksentry(internal->mutex);
    return internal->get_mset(first, maxitems, omrset, moptions, mdecider);
}

OmESet
OmEnquire::get_eset(om_termcount maxitems,
                    const OmRSet & omrset,
	            const OmExpandOptions * eoptions,
		    const OmExpandDecider * edecider) const
{
    OmLockSentry locksentry(internal->mutex);
    return internal->get_eset(maxitems, omrset, eoptions, edecider);
}

const OmDocument *
OmEnquire::get_doc(om_docid did) const
{
    OmLockSentry locksentry(internal->mutex);
    return internal->get_doc(did);
}

const OmDocument *
OmEnquire::get_doc(const OmMSetItem &mitem) const
{
    OmLockSentry locksentry(internal->mutex);
    return internal->get_doc(mitem);
}

om_termname_list
OmEnquire::get_matching_terms(const OmMSetItem &mitem) const
{
    OmLockSentry locksentry(internal->mutex);
    return internal->get_matching_terms(mitem);
}

om_termname_list
OmEnquire::get_matching_terms(om_docid did) const
{
    OmLockSentry locksentry(internal->mutex);
    return internal->get_matching_terms(did);
}
