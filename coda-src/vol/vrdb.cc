/* BLURB gpl

                           Coda File System
                              Release 5

          Copyright (c) 1987-1999 Carnegie Mellon University
                  Additional copyrights listed below

This  code  is  distributed "AS IS" without warranty of any kind under
the terms of the GNU General Public Licence Version 2, as shown in the
file  LICENSE.  The  technical and financial  contributors to Coda are
listed in the file CREDITS.

                        Additional copyrights
                           none currently

#*/

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <sys/types.h>
#include "coda_string.h"
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <struct.h>
#include <codadir.h>

#ifdef __cplusplus
}
#endif

#include <volume.h>
#include <srv.h>
#include <util.h>
#include "vrdb.h"

vrtab VRDB("VRDB");

/* hash function for the vrdb hash table - just return the id */
int vrtabHashfn(void *id) {
    return((int)id);
}

int nametabHashfn(void *p) {
    int length = strlen((char *)p);
    int hash = 0;
    for (int i = 0; i < length; i++)
	hash += (int)(((char *)p)[i]);
    return(hash);
}

vrtab::vrtab(char *n) : ohashtab(VRTABHASHSIZE, vrtabHashfn), 
    namehtb(VRTABHASHSIZE, nametabHashfn)
{
    name = strdup(n);
}


vrtab::~vrtab() {
    clear();
    free(name);
}


void vrtab::add(vrent *vre) {
  ohashtab::insert((void *)vre->volnum, vre);
  namehtb.insert((void *)vre->key, &vre->namehtblink);

#ifdef MULTICAST
    if (vre->index(ThisHostAddr) != -1) 
	JoinedVSGs.join(vre->addr);
#endif
}


void vrtab::remove(vrent *vre) {
    ohashtab::remove((void *)vre->volnum, vre);
    namehtb.remove(vre->key, &vre->namehtblink);
      
    delete vre;
}


vrent *vrtab::find(VolumeId grpvolnum) {
    ohashtab_iterator next(*this, (void *)grpvolnum);
    vrent *vre;

    while ((vre = (vrent *)next()))
	if (vre->volnum == grpvolnum) return(vre);

    return(0);
}


vrent *vrtab::find(char *grpname) {
    ohashtab_iterator next(namehtb, (void *)grpname);
    vrent *vre;
    olink *l;
    
    while ((l = next())) {
	vre = strbase(vrent, l, namehtblink);
	if (STREQ(vre->key, grpname)) return(vre);
    }
    return(0);
}


vrent *vrtab::ReverseFind(VolumeId rwvolnum, int *idx)
{
    if (rwvolnum == 0) return(0);

    ohashtab_iterator next(*this, (void *) -1);
    vrent *vre;

    while ((vre = (vrent *)next())) {
	for (int i = 0; i < vre->nServers; i++) {
	    if (vre->ServerVolnum[i] == rwvolnum) {
		if (idx) *idx = i;
		return(vre);
	    }
	}
    }

    return(0);
}


void vrtab::clear() {
    ohashtab_iterator next(*this, (void *)-1);
    vrent *vre;

    while ((vre = (vrent *)next())) {
	ohashtab::remove((void *)vre->volnum, vre);
	namehtb.remove(vre->key, &(vre->namehtblink));
	delete vre;
    }
}


void vrtab::print() {
    print(stdout);
}


void vrtab::print(FILE *fp) {
    fflush(fp);
    print(fileno(fp));
}


void vrtab::print(int afd) {
    char buf[40];
    sprintf(buf, "%#08lx : %-16s\n", (long)this, name);
    write(afd, buf, strlen(buf));

    ohashtab_iterator next(*this, (void *)-1);
    vrent *vre;
    while ((vre = (vrent *)next())) 
	    vre->print(afd);
}

int vrtab::dump(int afd)
{
    ohashtab_iterator next(*this, (void *)-1);
    vrent *vre;
    while ((vre = (vrent *)next())) 
	    if (vre->dump(afd) == -1)
		return -1;
    return 0;
}

void CheckVRDB() {
    int VRDB_fd = open(VRDB_PATH, O_RDONLY, 0);
    if (VRDB_fd < 0) {
	LogMsg(0, VolDebugLevel, stdout, "CheckVRDB: could not open VRDB");
	return;
    }

    VRDB.clear();
#ifdef MULTICAST
    JoinedVSGs.UnMark();
#endif

    /* Build the new VRDB. */
    vrent vre;
    while (read(VRDB_fd, &vre, sizeof(vrent)) == sizeof(vrent)) {
        vre.ntoh();
	VRDB.add(new vrent(vre));
    }

    close(VRDB_fd);

#ifdef MULTICAST
    JoinedVSGs.GarbageCollect();
#endif

}

int DumpVRDB(int outfd)
{
    return VRDB.dump(outfd);
}


int XlateVid(VolumeId *vidp, int *count, int *pos) 
{
    vrent *vre = VRDB.find(*vidp);
    if (!vre) return(0);

    int ix;

    if ((ix = vre->index(ThisHostAddr)) == -1) return(0);


    *vidp = vre->ServerVolnum[ix];
    if (count) *count = vre->nServers;
    if (pos)   *pos = ix;
    return(1);
}


int ReverseXlateVid(VolumeId *vidp, int *idx)
{
    vrent *vre = VRDB.ReverseFind(*vidp, idx);
    if (!vre) return(0);

    *vidp = vre->volnum;
    return(1);
}

vrent::vrent() {
    memset(key, 0, sizeof(key));
    volnum = 0;
    nServers = 0;
    memset(ServerVolnum, 0, sizeof(ServerVolnum));
    dontuse_vsgaddr = 0;
}


vrent::vrent(vrent& vre) {
    strcpy(key, vre.key);
    volnum = vre.volnum;
    nServers = vre.nServers;
    memcpy(ServerVolnum, vre.ServerVolnum, sizeof(ServerVolnum));
    dontuse_vsgaddr = vre.dontuse_vsgaddr;
}


int vrent::operator=(vrent& vre) {
    abort();
    return(0);	/* keep C++ happy */
}


vrent::~vrent() {
}


void vrent::GetHosts(unsigned long *Hosts) {
    memset(Hosts, 0, VSG_MEMBERS * sizeof(unsigned long));

    for (int i = 0; i < nServers; i++)
	Hosts[i] = VolToHostAddr(ServerVolnum[i]);
}


int vrent::index(unsigned long hostaddr) {
    for (int i = 0; i < nServers; i++)
	if (hostaddr == VolToHostAddr(ServerVolnum[i])) return(i);

    return(-1);
}


void vrent::HostListToVV(unsigned long *Hosts, vv_t *VV) {
    memset((void *)VV, 0, sizeof(vv_t));
    for (int i = 0; i < VSG_MEMBERS; i++)
	if (Hosts[i]) {
	    int ix = index(Hosts[i]);
	    CODA_ASSERT(ix != -1);
	    (&(VV->Versions.Site0))[ix] = 1;
	}
}


int vrent::GetVolumeInfo(VolumeInfo *Info) {
    int i;

    if (nServers <= 0 || nServers > VSG_MEMBERS) {
	LogMsg(0, VolDebugLevel, stdout, "vrent::GetVolumeInfo: bogus nServers (%d)", nServers);
	return(VNOVOL);
    }

    memset((void *)Info, 0, sizeof(VolumeInfo));
    Info->Vid = volnum;
    Info->Type = REPVOL;
    (&Info->Type0)[REPVOL] = volnum;
    Info->ServerCount = nServers;
    for (i = 0; i < nServers; i++) {
	unsigned long hostaddr = VolToHostAddr(ServerVolnum[i]);
	if (hostaddr == 0) {
	    LogMsg(0, VolDebugLevel, stdout, "vrent::GetVolumeInfo: no hostaddr for volume (%lx)",
		    ServerVolnum[i]);
	    return(VNOVOL);
	}
	(&Info->Server0)[i] = hostaddr;
    }
    Info->VSGAddr = dontuse_vsgaddr;
    for (i = 0; i < nServers; i++)
	(&Info->RepVolMap.Volume0)[i] = ServerVolnum[i];

    return(0);
}

void vrent::hton() {
    /* we won't translate the key on the hopes that strings take care of themselves */
    volnum = htonl(volnum);
    /* Don't need to translate nServers, it is a byte */

    for (int i = 0; i < VSG_MEMBERS; i++)
      ServerVolnum[i] = htonl(ServerVolnum[i]);

    dontuse_vsgaddr = htonl(dontuse_vsgaddr);
}

void vrent::ntoh() {
    /* we won't translate the key on the hopes that strings take care of themselves */
    volnum = ntohl(volnum);
    /* Don't need to translate nServers, it is a byte */

    for (int i = 0; i < VSG_MEMBERS; i++)
      ServerVolnum[i] = ntohl(ServerVolnum[i]);

    dontuse_vsgaddr = ntohl(dontuse_vsgaddr);
}

void vrent::print() {
    print(stdout);
}


void vrent::print(FILE *fp) {
    fflush(fp);
    print(fileno(fp));
}


void vrent::print(int afd) {
    char buf[512];
    sprintf(buf, "%p : %s : 0x%lx, %d, (x.x.x.x.x.x.x.x), 0x%lx\n",
	    this, key, volnum, nServers, dontuse_vsgaddr);
    write(afd, buf, strlen(buf));

}

int vrent::dump(int afd)
{
    char buf[512];
    int i, n, len;

    len = sprintf(buf, "%s %lx %d ", key, volnum, nServers);

    for (i = 0; i < VSG_MEMBERS; i++) {
	n = sprintf(buf + len, "%lx ", ServerVolnum[i]);
	len += n;
    }
    n = sprintf(buf + len, "%lX\n", dontuse_vsgaddr);
    len += n;

    n = write(afd, buf, len);
    if (n != len)
	return -1;

    return 0;
}
