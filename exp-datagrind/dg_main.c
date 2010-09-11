
/*--------------------------------------------------------------------*/
/*--- Datagrind: Track data accesses.               dg_main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Datagrind, a tool for tracking data accesses.
   It borrows heavily from the Lackey tool.

   Copyright (C) 2010 Bruce Merry
      bmerry@users.sourceforge.net

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include "pub_tool_basics.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_vki.h"
#include "pub_tool_hashtable.h"
#include "pub_tool_xarray.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_libcfile.h"
#include "pub_tool_debuginfo.h"
#include "pub_tool_stacktrace.h"
#include "pub_tool_replacemalloc.h"
#include "pub_tool_options.h"
#include "pub_tool_machine.h"
#include "pub_tool_threadstate.h"

#include "datagrind.h"
#include "dg_record.h"

#define STACK_DEPTH 4
#define NEVENTS 4
#define OUT_BUF_SIZE 4096

typedef enum
{
   EventType_Dr,
   EventType_Dw,
   EventType_Ir
} EventType;

typedef struct
{
   EventType type;
   IRExpr *addr;
   Int size;
} Event;

typedef struct
{
   VgHashNode header;
} DgDebugInfo;

typedef struct
{
   VgHashNode header;
   SizeT szB;
   SizeT actual_szB;
   UInt n_ips;
   StackTrace ips;
} DgMallocBlock;

typedef struct
{
   HWord addr;
   UChar size;
} DgBBDefInstr;

typedef struct
{
   UChar dir;
   UChar size;
   UChar iseq;
} DgBBDefAccess;

typedef struct
{
   XArray *instrs;
   XArray *accesses;
} DgBBDef;

typedef struct
{
   UInt n_ips;
   Addr ips[STACK_DEPTH];
   HWord n_instrs;
   XArray *accesses; /* HWord */
} DgBBRun;

static Int out_fd = -1;
static UChar out_buf[OUT_BUF_SIZE];
static UInt out_buf_used = 0;
static DgBBRun out_bbr;

static VgHashTable debuginfo_table = NULL;
static Bool debuginfo_dirty = True;

static VgHashTable block_table = NULL;

static Char *clo_datagrind_out_file = "datagrind.out.%p";

static Bool dg_process_cmd_line_option(Char *arg)
{
   if (VG_STR_CLO(arg, "--datagrind-out-file", clo_datagrind_out_file)) {}
   else if (VG_(replacement_malloc_process_cmd_line_option)(arg)) {}
   else
       return False;
   return True;
}

static void dg_print_usage(void)
{
   VG_(printf)(
"    --datagrind-out-file=<file>      output file name [datagrind.out]\n"
   );
}

static void dg_print_debug_usage(void)
{
   VG_(printf)(
"    (none)\n"
   );
}

static void out_flush(void)
{
   VG_(write)(out_fd, out_buf, out_buf_used);
   out_buf_used = 0;
}

static void out_bytes(const void *buf, UInt count)
{
   if (count > OUT_BUF_SIZE - out_buf_used)
      out_flush();
   VG_(memcpy)(out_buf + out_buf_used, buf, count);
   out_buf_used += count;
}

static void out_byte(UChar byte)
{
   if (out_buf_used >= OUT_BUF_SIZE)
      out_flush();
   out_buf[out_buf_used++] = byte;
}

static inline void out_word(UWord word)
{
   out_bytes(&word, sizeof(word));
}

static void out_length(ULong len)
{
   if (len < 255)
      out_byte((UChar) len);
   else
   {
      out_byte(255);
      out_bytes(&len, sizeof(len));
   }
}

static void prepare_out_file(void)
{
   SysRes sres;
   Char *filename = VG_(expand_file_name)("--datagrind-out-file", clo_datagrind_out_file);
   sres = VG_(open)(filename, VKI_O_CREAT | VKI_O_TRUNC | VKI_O_WRONLY,
                    VKI_S_IRUSR | VKI_S_IWUSR);
   if (sr_isError(sres))
   {
      VG_(message)(Vg_UserMsg,
                   "Error: can not open datagrind output file `%s'\n",
                   filename);
      VG_(exit)(1);
   }
   else
   {
      static const Char magic[] = "DATAGRIND1";

      out_fd = (Int) sr_Res(sres);
      out_byte(DG_R_HEADER);
      out_byte(sizeof(magic) + 3);
      out_bytes(magic, sizeof(magic));
      out_byte(1); /* version */
#if VG_BIGENDIAN
      out_byte(1);
#elif VG_LITTLEENDIAN
      out_byte(0);
#else
      tl_assert(0);
#endif
      out_byte(VG_WORDSIZE);
   }
}

static void dg_post_clo_init(void)
{
   debuginfo_table = VG_(HT_construct)("datagrind.debuginfo_table");
   block_table = VG_(HT_construct)("datagrind.block_table");
   out_bbr.n_instrs = 0;
   out_bbr.n_ips = 0;
   out_bbr.accesses = VG_(newXA)(VG_(malloc), "datagrind.out_bb.accesses",
                                 VG_(free), sizeof(HWord));

   prepare_out_file();
}

static void trace_bb_flush(DgBBRun *bbr)
{
   if (bbr->n_instrs > 0)
   {
      UInt i;
      Word n_accesses = VG_(sizeXA)(bbr->accesses);
      ULong length = 2 + (bbr->n_ips + n_accesses) * sizeof(HWord);

      tl_assert(bbr->n_ips > 0);

      out_byte(DG_R_BBRUN);
      out_length(length);
      out_byte(bbr->n_ips);
      for (i = 0; i < bbr->n_ips; i++)
         out_word(bbr->ips[i]);
      out_byte(bbr->n_instrs);
      for (i = 0; i < n_accesses; i++)
         out_word(*(HWord *) VG_(indexXA)(bbr->accesses, i));

      /* Reset for next */
      bbr->n_instrs = 0;
      bbr->n_ips = 0;
      VG_(dropTailXA)(bbr->accesses, n_accesses);
   }
}

static VG_REGPARM(1) void trace_bb_start(Addr iaddr)
{
   DgBBRun *bbr = &out_bbr;
   ThreadId tid = VG_(get_running_tid)();
   HWord ip = VG_(get_IP)(tid);

   trace_bb_flush(bbr);
   bbr->n_ips = VG_(get_StackTrace)(tid, bbr->ips,
                                    STACK_DEPTH, NULL, NULL,
                                    iaddr - ip);
   tl_assert(bbr->n_ips > 0);
   tl_assert(iaddr == bbr->ips[0]);
}

static VG_REGPARM(1) void trace_access(Addr addr)
{
   VG_(addToXA)(out_bbr.accesses, &addr);
}

static VG_REGPARM(1) void trace_update_instrs(HWord n_instrs)
{
   out_bbr.n_instrs = n_instrs;
}

static void clean_debuginfo(void)
{
   if (debuginfo_dirty)
   {
      const DebugInfo *di = VG_(next_DebugInfo)(NULL);

      for (; di != NULL; di = VG_(next_DebugInfo)(di))
      {
         const UChar *filename = VG_(DebugInfo_get_filename)(di);
         Addr text_avma = VG_(DebugInfo_get_text_avma)(di);
         SizeT filename_len = VG_(strlen)(filename);

         if (!VG_(HT_lookup)(debuginfo_table, (UWord) di))
         {
            DgDebugInfo *node = VG_(calloc)("debuginfo_table.node", 1, sizeof(DgDebugInfo));
            node->header.key = (UWord) di;
            VG_(HT_add_node)(debuginfo_table, node);

            if (filename_len > 128) filename_len = 128;
            out_byte(DG_R_TEXT_AVMA);
            out_byte(filename_len + sizeof(Addr) + 1);
            out_word(text_avma);
            out_bytes(filename, filename_len);
            out_byte('\0');
         }
      }
      debuginfo_dirty = False;
   }
}

static void dg_bbdef_init(DgBBDef *bbd)
{
   bbd->instrs = VG_(newXA)(VG_(malloc), "datagrind.instrs", VG_(free), sizeof(DgBBDefInstr));
   bbd->accesses = VG_(newXA)(VG_(malloc), "datagrind.accesses", VG_(free), sizeof(DgBBDefAccess));
}

static void dg_bbdef_flush(DgBBDef *bbd)
{
   Word n_instrs = VG_(sizeXA)(bbd->instrs);
   Word n_accesses = VG_(sizeXA)(bbd->accesses);
   ULong len = 1 + sizeof(HWord) + (1 + sizeof(HWord)) * n_instrs + 3 * n_accesses;
   Word i;

   if (n_instrs == 0)
      return;
   tl_assert(n_instrs <= 255);

   out_byte(DG_R_BBDEF);
   out_length(len);
   out_byte(n_instrs);
   out_word(n_accesses);
   for (i = 0; i < n_instrs; i++)
   {
      DgBBDefInstr *instr = (DgBBDefInstr *) VG_(indexXA)(bbd->instrs, i);
      out_word(instr->addr);
      out_byte(instr->size);
   }
   for (i = 0; i < n_accesses; i++)
   {
      DgBBDefAccess *access = (DgBBDefAccess *) VG_(indexXA)(bbd->accesses, i);
      out_byte(access->dir);
      out_byte(access->size);
      out_byte(access->iseq);
   }

   /* Empty the arrays */
   VG_(dropTailXA)(bbd->instrs, n_instrs);
   VG_(dropTailXA)(bbd->accesses, n_accesses);
}

static void dg_bbdef_destroy(DgBBDef *bbd)
{
   VG_(deleteXA)(bbd->instrs);
   VG_(deleteXA)(bbd->accesses);
}

static void dg_bbdef_add_instr(IRSB *sbOut, DgBBDef *bbd, HWord addr, SizeT size)
{
   DgBBDefInstr instr;

   if (VG_(sizeXA)(bbd->instrs) == 255)
      dg_bbdef_flush(bbd);

   if (VG_(sizeXA)(bbd->instrs) == 0)
   {
      /* Start of internal BB, so inject code to grab stack trace */
      IRDirty* di;
      IRExpr** argv;

      argv = mkIRExprVec_1(mkIRExpr_HWord(addr));
      /* TODO: does this need to marked as reading guest state and memory, for
       * stack unwinding?
       */
      di = unsafeIRDirty_0_N(1, (Char *) "trace_bb_start",
                             VG_(fnptr_to_fnentry)(&trace_bb_start), argv);
      addStmtToIRSB(sbOut, IRStmt_Dirty(di));
   }

   tl_assert(size <= 255);
   instr.addr = addr;
   instr.size = (UChar) size;
   VG_(addToXA)(bbd->instrs, &instr);
}

static void dg_bbdef_add_access(IRSB *sbOut, DgBBDef *bbd, UChar dir, IRExpr *addr, SizeT size)
{
   SizeT n_instrs = VG_(sizeXA)(bbd->instrs);
   DgBBDefAccess access;
   IRDirty* di;
   IRExpr** argv;

   tl_assert(n_instrs > 0);
   tl_assert(size <= 255);
   access.dir = dir;
   access.size = size;
   access.iseq = n_instrs - 1;
   VG_(addToXA)(bbd->accesses, &access);

   argv = mkIRExprVec_1(addr);
   di = unsafeIRDirty_0_N(1, (Char *) "trace_access",
                          VG_(fnptr_to_fnentry)(&trace_access), argv);
   addStmtToIRSB(sbOut, IRStmt_Dirty(di));
}

/* Adds IR to update the instruction count. Must be done before an exit
 * from a block.
 */
static void dg_bbdef_update_instrs(IRSB *sbOut, DgBBDef *bbd)
{
   IRDirty* di;
   IRExpr** argv;
   SizeT n_instrs = VG_(sizeXA)(bbd->instrs);

   if (n_instrs == 0)
      return;

   argv = mkIRExprVec_1(mkIRExpr_HWord(n_instrs));
   di = unsafeIRDirty_0_N(1, (Char *) "trace_update_instrs",
                          VG_(fnptr_to_fnentry)(&trace_update_instrs), argv);
   addStmtToIRSB(sbOut, IRStmt_Dirty(di));
}

static IRSB* dg_instrument(VgCallbackClosure* closure,
                           IRSB* sbIn,
                           VexGuestLayout* layout,
                           VexGuestExtents* vge,
                           IRType gWordTy, IRType hWordTy)
{
   IRSB* sbOut;
   Int i;
   DgBBDef bbd;
   Bool needs_flush = False;

   if (gWordTy != hWordTy)
   {
      /* We don't currently support this case. */
      VG_(tool_panic)("host/guest word size mismatch");
   }

   clean_debuginfo();

   sbOut = deepCopyIRSBExceptStmts(sbIn);

   /* Copy preamble */
   for (i = 0; i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark; i++)
   {
      addStmtToIRSB(sbOut, sbIn->stmts[i]);
   }

   dg_bbdef_init(&bbd);

   for (; i < sbIn->stmts_used; i++)
   {
      IRStmt *st = sbIn->stmts[i];
      if (!st || st->tag == Ist_NoOp) continue;

      switch (st->tag)
      {
         case Ist_NoOp:
         case Ist_AbiHint:
         case Ist_Put:
         case Ist_PutI:
         case Ist_MBE:
            addStmtToIRSB(sbOut, st);
            break;
         case Ist_Exit:
            dg_bbdef_update_instrs(sbOut, &bbd);
            /* TODO: needed when crossing function boundaries */
            /* needs_flush = True; */
            addStmtToIRSB(sbOut, st);
            break;
         case Ist_IMark:
            if (needs_flush)
            {
               dg_bbdef_flush(&bbd);
               needs_flush = False;
            }
            addStmtToIRSB(sbOut, st);
            dg_bbdef_add_instr(sbOut, &bbd, st->Ist.IMark.addr, st->Ist.IMark.len);
            break;
         case Ist_WrTmp:
            {
               IRExpr* data = st->Ist.WrTmp.data;
               if (data->tag == Iex_Load)
               {
                  dg_bbdef_add_access(sbOut, &bbd, DG_ACC_READ,
                                      data->Iex.Load.addr,
                                      sizeofIRType(data->Iex.Load.ty));
               }
            }
            addStmtToIRSB(sbOut, st);
            break;
         case Ist_Store:
            {
               IRExpr* data = st->Ist.Store.data;
               dg_bbdef_add_access(sbOut, &bbd, DG_ACC_WRITE,
                                    st->Ist.Store.addr,
                                    sizeofIRType(typeOfIRExpr(sbOut->tyenv, data)));
            }
            addStmtToIRSB(sbOut, st);
            break;
         case Ist_Dirty:
            {
               IRDirty* d = st->Ist.Dirty.details;
               if (d->mFx != Ifx_None)
               {
                  tl_assert(d->mAddr != NULL);
                  tl_assert(d->mSize != 0);
                  if (d->mFx == Ifx_Read || d->mFx == Ifx_Modify)
                     dg_bbdef_add_access(sbOut, &bbd, DG_ACC_READ,
                                          d->mAddr, d->mSize);
                  if (d->mFx == Ifx_Write || d->mFx == Ifx_Modify)
                     dg_bbdef_add_access(sbOut, &bbd, DG_ACC_WRITE,
                                          d->mAddr, d->mSize);
               }
            }
            addStmtToIRSB(sbOut, st);
            break;
         case Ist_CAS:
            {
               Int dataSize;
               IRCAS *cas = st->Ist.CAS.details;
               tl_assert(cas->addr != NULL);
               tl_assert(cas->dataLo != NULL);
               dataSize = sizeofIRType(typeOfIRExpr(sbOut->tyenv, cas->dataLo));
               if (cas->dataHi != NULL)
                  dataSize *= 2;
               dg_bbdef_add_access(sbOut, &bbd, DG_ACC_READ, cas->addr, dataSize);
               dg_bbdef_add_access(sbOut, &bbd, DG_ACC_WRITE, cas->addr, dataSize);
            }
            addStmtToIRSB(sbOut, st);
            break;
         default:
            tl_assert(0);
      }
   }

   dg_bbdef_update_instrs(sbOut, &bbd);
   dg_bbdef_flush(&bbd);
   dg_bbdef_destroy(&bbd);

   return sbOut;
}

static void out_add_block(DgMallocBlock* block)
{
   UInt i;
   out_byte(DG_R_MALLOC_BLOCK);
   out_byte((block->n_ips + 3) * sizeof(Addr));
   out_word(block->header.key); /* addr */
   out_word(block->szB);
   out_word(block->n_ips);

   for (i = 0; i < block->n_ips; i++)
      out_word(block->ips[i]);
}

static void out_remove_block(DgMallocBlock* block)
{
   out_byte(DG_R_FREE_BLOCK);
   out_byte(sizeof(Addr));
   out_word(block->header.key); /* addr */
}

static void add_block(ThreadId tid, void* p, SizeT szB, Bool custom)
{
   StackTrace ips = VG_(calloc)("datagrind.add_block.stacktrace", STACK_DEPTH, sizeof(Addr));
   DgMallocBlock* block = VG_(calloc)("datagrind.add_block.block", 1, sizeof(DgMallocBlock));

   block->header.key = (UWord) p;
   block->szB = szB;
   if (!custom)
      block->actual_szB = VG_(malloc_usable_size)(p);
   else
      block->actual_szB = szB;

   block->n_ips = VG_(get_StackTrace)(tid, ips, STACK_DEPTH, NULL, NULL, 0);
   block->ips = ips;

   VG_(HT_add_node)(block_table, block);

   out_add_block(block);
}

/* Returns true if the block was found */
static Bool remove_block(void* p)
{
   DgMallocBlock* block = VG_(HT_remove)(block_table, (UWord) p);
   if (block == NULL)
      return False;

   out_remove_block(block);

   VG_(free)(block->ips);
   VG_(free)(block);
   return True;
}

static void* dg_malloc(ThreadId tid, SizeT szB)
{
   void* p = VG_(cli_malloc)(VG_(clo_alignment), szB);
   if (p != NULL)
      add_block(tid, p, szB, False);
   return p;
}

static void* dg_calloc(ThreadId tid, SizeT m, SizeT szB)
{
   if (m > (~(SizeT) 0) / szB)
      return NULL;
   else
   {
      void* p;
      szB *= m;
      p = VG_(cli_malloc)(VG_(clo_alignment), szB);
      if (p != NULL)
      {
         VG_(memset)(p, 0, szB);
         add_block(tid, p, szB, False);
      }
      return p;
   }
}

static void* dg_memalign(ThreadId tid, SizeT alignB, SizeT szB)
{
   void* p = VG_(cli_malloc)(alignB, szB);
   if (p != NULL)
      add_block(tid, p, szB, False);
   return p;
}

static void dg_free(ThreadId tid, void* p)
{
   if (remove_block(p))
      VG_(cli_free)(p);
}

static void* dg_realloc(ThreadId tid, void* p, SizeT szB)
{
   DgMallocBlock* block = VG_(HT_lookup)(block_table, (UWord) p);

   if (block == NULL)
      return NULL;  /* bogus realloc - the wrapper handles the corner cases */

   if (szB <= block->actual_szB)
   {
      /* No need to resize. */
      out_remove_block(block);
      block->szB = szB;
      block->n_ips = VG_(get_StackTrace)(tid, block->ips, STACK_DEPTH, NULL, NULL, 0);
      out_add_block(block);
      return p;
   }
   else
   {
      /* New size is bigger */
      void* new_p = VG_(cli_malloc)(VG_(clo_alignment), szB);
      if (new_p == NULL)
         return NULL;
      VG_(memcpy)(new_p, p, block->szB);

      out_remove_block(block);
      VG_(HT_remove)(block_table, (UWord) p);

      block->header.key = (UWord) new_p;
      block->szB = szB;
      block->actual_szB = VG_(malloc_usable_size)(new_p);
      block->n_ips = VG_(get_StackTrace)(tid, block->ips, STACK_DEPTH, NULL, NULL, 0);

      VG_(HT_add_node)(block_table, block);
      out_add_block(block);
      return new_p;
   }
}

static SizeT dg_malloc_usable_size(ThreadId tid, void* p)
{
   DgMallocBlock* block = VG_(HT_lookup)(block_table, (UWord) p);
   if (block == NULL)
      return 0;
   else
      return block->actual_szB;
}

static Bool dg_handle_client_request(ThreadId tid, UWord *args, UWord *ret)
{
   switch (args[0])
   {
   case VG_USERREQ__MALLOCLIKE_BLOCK:
      {
         void* p = (void*) args[1];
         SizeT szB = args[2];
         add_block(tid, p, szB, True);
      }
      break;
   case VG_USERREQ__FREELIKE_BLOCK:
      {
         void* p = (void*) args[1];
         remove_block(p);
      }
      break;
   case VG_USERREQ__TRACK_RANGE:
      {
         UWord addr = args[1];
         UWord len = args[2];
         const Char* type = (const Char*) args[3];
         const Char* label = (const Char*) args[4];
         SizeT type_len = VG_(strlen)(type);
         SizeT label_len = VG_(strlen)(label);

         if (type_len > 64) type_len = 64;
         if (label_len > 64) label_len = 64;
         out_byte(DG_R_TRACK_RANGE);
         out_byte(2 * sizeof(addr) + type_len + label_len + 2);
         out_word(addr);
         out_word(len);
         out_bytes(type, type_len);
         out_byte('\0');
         out_bytes(label, label_len);
         out_byte('\0');
      }
      break;
   case VG_USERREQ__UNTRACK_RANGE:
      {
          UWord addr = args[1];
          UWord len = args[2];
          out_byte(DG_R_UNTRACK_RANGE);
          out_byte(2 * sizeof(addr));
          out_word(addr);
          out_word(len);
      }
      break;
   case VG_USERREQ__START_EVENT:
   case VG_USERREQ__END_EVENT:
      {
          const Char *label = (const Char *) args[1];
          SizeT label_len = VG_(strlen)(label);
          if (label_len > 64) label_len = 64;
          out_byte(args[0] == VG_USERREQ__START_EVENT ? DG_R_START_EVENT : DG_R_END_EVENT);
          out_byte(label_len + 1);
          out_bytes(label, label_len);
          out_byte('\0');
      }
      break;
   default:
      *ret = 0;
      return False;
   }
   *ret = 0;
   return True;
}

static void dg_track_new_mem_mmap_or_startup(Addr a, SizeT len, Bool rr, Bool ww, Bool xx, ULong di_handle)
{
   if (xx)
      debuginfo_dirty = True;
}

static void dg_fini(Int exitcode)
{
   trace_bb_flush(&out_bbr);
   VG_(deleteXA)(out_bbr.accesses);

   if (out_fd != -1)
   {
      out_flush();
      VG_(close)(out_fd);
   }

   /* TODO: need to free the node entries */
   if (debuginfo_table != NULL)
      VG_(HT_destruct)(debuginfo_table);
}

static void dg_pre_clo_init(void)
{
   VG_(details_name)            ("Datagrind");
   VG_(details_version)         (NULL);
   VG_(details_description)     ("tracks data accesses");
   VG_(details_copyright_author)(
      "Copyright (C) 2010, and GNU GPL'd, by Bruce Merry.");
   VG_(details_bug_reports_to)  ("bmerry@users.sourceforge.net");

   VG_(basic_tool_funcs)        (dg_post_clo_init,
                                 dg_instrument,
                                 dg_fini);

   VG_(needs_command_line_options)(dg_process_cmd_line_option,
                                   dg_print_usage,
                                   dg_print_debug_usage);
   VG_(needs_client_requests)(dg_handle_client_request);
   VG_(needs_malloc_replacement)(
      dg_malloc,         /* malloc */
      dg_malloc,         /* __builtin_new */
      dg_malloc,         /* __builtin_new */
      dg_memalign,       /* memalign */
      dg_calloc,         /* calloc */
      dg_free,           /* free */
      dg_free,           /* __builtin_delete */
      dg_free,           /* __builtin_vec_delete */
      dg_realloc,        /* realloc */
      dg_malloc_usable_size,
      0                  /* red zone */
      );

   VG_(track_new_mem_startup)(dg_track_new_mem_mmap_or_startup);
   VG_(track_new_mem_mmap)(dg_track_new_mem_mmap_or_startup);
}

VG_DETERMINE_INTERFACE_VERSION(dg_pre_clo_init)
