/*****************************************************************************
 * Copyright (C) 2013-2020 MulticoreWare, Inc
 *
 * Authors: Steve Borho <steve@borho.org>
 *          Min Chen <chenm003@163.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at license @ x265.com.
 *****************************************************************************/

#ifndef X265_SLICETYPE_H
#define X265_SLICETYPE_H

#include "common.h"
#include "slice.h"
#include "motion.h"
#include "piclist.h"
#include "threadpool.h"
#include "temporalfilter.h"

#include <libfork.hpp>
#include <libfork/schedule/lazy_pool.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace X265_NS {
// private namespace

struct Lowres;
class Frame;
class Lookahead;
class ForkJoinPool;

#define LOWRES_COST_MASK  ((1 << 14) - 1)
#define LOWRES_COST_SHIFT 14
#define AQ_EDGE_BIAS 0.5
#define EDGE_INCLINATION 45
#define TEMPORAL_SCENECUT_THRESHOLD 50

#define X265_ABS(a)                        (((a) < 0) ? (-(a)) : (a))

#define PICTURE_DIFF_VARIANCE_TH            390
#define PICTURE_VARIANCE_TH                 1500
#define LOW_VAR_SCENE_CHANGE_TH             2250
#define HIGH_VAR_SCENE_CHANGE_TH            3500

#define PICTURE_DIFF_VARIANCE_CHROMA_TH     10
#define PICTURE_VARIANCE_CHROMA_TH          20
#define LOW_VAR_SCENE_CHANGE_CHROMA_TH      2250/4
#define HIGH_VAR_SCENE_CHANGE_CHROMA_TH     3500/4

#define FLASH_TH                            1.5
#define FADE_TH                             4
#define INTENSITY_CHANGE_TH                 4

#define NUM64x64INPIC(w,h)                  ((w*h)>> (MAX_LOG2_CU_SIZE<<1))

#define MOTION_ESTIMATION_LEVELS            4
#define PARALLEL_ME_ROWSIZE                 16

#if HIGH_BIT_DEPTH
#define EDGE_THRESHOLD 1023.0
#else
#define EDGE_THRESHOLD 255.0
#endif
#define PI 3.14159265

/* Thread local data for lookahead tasks */
struct LookaheadTLD
{
    MotionEstimate  me;
    pixel*          wbuffer[4];
    int             widthInCU;
    int             heightInCU;
    int             ncu;
    int             paddedLines;

    /* Aligned scratch for estimateCUCost(); lives here rather than on the
     * coroutine frame because coroutine frames do not guarantee over-aligned
     * locals */
    ALIGN_VAR_32(pixel, subpelbuf0[X265_LOWRES_CU_SIZE * X265_LOWRES_CU_SIZE]);
    ALIGN_VAR_32(pixel, subpelbuf1[X265_LOWRES_CU_SIZE * X265_LOWRES_CU_SIZE]);
    ALIGN_VAR_32(pixel, avgbuf[X265_LOWRES_CU_SIZE * X265_LOWRES_CU_SIZE]);

#if DETAILED_CU_STATS
    int64_t         framecostBatchElapsedTime;
    int64_t         coopSliceElapsedTime;
    int64_t         mcstfBatchElapsedTime;
#endif

    LookaheadTLD()
    {
        me.init(X265_CSP_I400);
        me.setQP(X265_LOOKAHEAD_QP);
        for (int i = 0; i < 4; i++)
            wbuffer[i] = NULL;
        widthInCU = heightInCU = ncu = paddedLines = 0;

#if DETAILED_CU_STATS
        framecostBatchElapsedTime = 0;
        coopSliceElapsedTime = 0;
        mcstfBatchElapsedTime = 0;
#endif
    }

    void init(int w, int h, int n)
    {
        widthInCU = w;
        heightInCU = h;
        ncu = n;
    }

    ~LookaheadTLD() { X265_FREE(wbuffer[0]); }

    void collectPictureStatistics(Frame *curFrame);
    void computeIntensityHistogramBinsLuma(Frame *curFrame, uint64_t *sumAvgIntensityTotalSegmentsLuma);

    void computeIntensityHistogramBinsChroma(
        Frame    *curFrame,
        uint64_t *sumAverageIntensityCb,
        uint64_t *sumAverageIntensityCr);

    void calculateHistogram(
        pixel    *inputSrc,
        uint32_t  inputWidth,
        uint32_t  inputHeight,
        intptr_t  stride,
        uint8_t   dsFactor,
        uint32_t *histogram,
        uint64_t *sum);

    void computePictureStatistics(Frame *curFrame);

    uint32_t calcVariance(pixel* src, intptr_t stride, intptr_t blockOffset, uint32_t plane);

    void calcAdaptiveQuantFrame(Frame *curFrame, x265_param* param);
    void calcFrameSegment(Frame *curFrame);
    void lowresIntraEstimate(Lowres& fenc, uint32_t qgSize);

    void weightsAnalyse(Lowres& fenc, Lowres& ref);
    void xPreanalyze(Frame* curFrame);
    void xPreanalyzeQp(Frame* curFrame);
protected:

    uint32_t acEnergyCu(Frame* curFrame, uint32_t blockX, uint32_t blockY, int csp, uint32_t qgSize);
    uint32_t edgeDensityCu(Frame* curFrame, uint32_t &avgAngle, uint32_t blockX, uint32_t blockY, uint32_t qgSize);
    uint32_t lumaSumCu(Frame* curFrame, uint32_t blockX, uint32_t blockY, uint32_t qgSize);
    uint32_t weightCostLuma(Lowres& fenc, Lowres& ref, WeightParam& wp);
    bool     allocWeightedRef(Lowres& fenc);
};

class Lookahead : public JobProvider
{
public:

    PicList       m_inputQueue;      // input pictures in order received
    PicList       m_outputQueue;     // pictures to be encoded, in encode order
    Lock          m_inputLock;
    Lock          m_outputLock;
    Event         m_outputSignal;
    LookaheadTLD* m_tld;
    x265_param*   m_param;
    Lowres*       m_lastNonB;
    int*          m_scratch;         // temp buffer for cutree propagate
    pixel*        m_noiseBlurBuf;    // persistent blur buffer for estimateNoise() fallback path
    int32_t*      m_gradMagBuf;      // persistent gradient-magnitude buffer for estimateNoise()
    bool          m_filterThisGOP;  // noise gate decision for the GOP currently being dispatched

    /* pre-lookahead */
    int           m_fullQueueSize;
    int           m_lastKeyframe;
    int           m_8x8Width;
    int           m_8x8Height;
    int           m_8x8Blocks;
    int           m_cuCount;
    int           m_numCoopSlices;
    int           m_numRowsPerSlice;
    int           m_inputCount;
    double        m_cuTreeStrength;

    /* HME */
    int           m_4x4Width;
    int           m_4x4Height;

    bool          m_isActive;
    bool          m_sliceTypeBusy;
    bool          m_bAdaptiveQuant;
    bool          m_outputSignalRequired;
    bool          m_bBatchMotionSearch;
    bool          m_bBatchFrameCosts;
    bool          m_bMcstfMotionSearch;
    bool          m_filled;
    bool          m_isSceneTransition;
    int           m_numPools;
    bool          m_extendGopBoundary;
    double        m_frameVariance[X265_BFRAME_MAX + 4];
    bool          m_isFadeIn;
    uint64_t      m_fadeCount;
    int           m_fadeStart;

    uint32_t    **m_accHistDiffRunningAvgCb;
    uint32_t    **m_accHistDiffRunningAvgCr;
    uint32_t    **m_accHistDiffRunningAvg;

    bool          m_resetRunningAvg;
    uint32_t      m_segmentCountThreshold;

    int8_t                  m_gopId;

    OrigPicBuffer*          m_origPicBuf;
    ForkJoinPool*           m_fjp;

    Lookahead(x265_param *param, ThreadPool *pool);
#if DETAILED_CU_STATS
    int64_t       m_slicetypeDecideElapsedTime;
    int64_t       m_preLookaheadElapsedTime;
    int64_t       m_framecostElapsedTime;
    int64_t       m_temporalFilterElapsedTime;
    uint64_t      m_countSlicetypeDecide;
    uint64_t      m_countPreLookahead;
    uint64_t      m_countFramecosts;
    uint64_t      m_countTemporalFilter;
    void          getWorkerStats(int64_t& framecostBatchElapsedTime, int64_t& coopSliceElapsedTime, int64_t& mcstfBatchElapsedTime);
#endif

    bool    create();
    void    destroy();
    void    stopJobs();

    void    addPicture(Frame&, int sliceType);
    void    addPicture(Frame& curFrame);
    void    checkLookaheadQueue(int &frameCnt);
    void    flush();
    Frame*  getDecidedPicture();

    void    getEstimatedPictureCost(Frame *pic);
    void    setLookaheadQueue();
    int     findSliceType(int poc);
    bool    generatemcstf(Frame * frame, PicList refPic, int poclast);
    bool    isFilterThisframe(uint8_t sliceTypeConfig, int curSliceType);
    int32_t estimateNoise(Frame* curFrame);


protected:

    void    findJob(int workerThreadID);
    void    slicetypeDecide();
    void    slicetypeAnalyse(Lowres **frames, bool bKeyframe);

    /* called by slicetypeAnalyse() to make slice decisions */
    bool    scenecut(Lowres **frames, int p0, int p1, bool bRealScenecut, int numFrames);
    bool    scenecutInternal(Lowres **frames, int p0, int p1, bool bRealScenecut);

    bool    histBasedScenecut(Lowres **frames, int p0, int p1, int numFrames);
    bool    detectHistBasedSceneChange(Lowres **frames, int p0, int p1, int p2);

    void    slicetypePath(Lowres **frames, int length, char(*best_paths)[X265_LOOKAHEAD_MAX + 1]);
    int64_t slicetypePathCost(Lowres **frames, char *path, int64_t threshold);
    int64_t vbvFrameCost(Lowres **frames, int p0, int p1, int b);
    void    vbvLookahead(Lowres **frames, int numFrames, int keyframes);
    void    aqMotion(Lowres **frames, bool bintra);
    void    calcMotionAdaptiveQuantFrame(Lowres **frames, int p0, int p1, int b);
    /* called by slicetypeAnalyse() to effect cuTree adjustments to adaptive
     * quant offsets */
    void    cuTree(Lowres **frames, int numframes, bool bintra);
    void    estimateCUPropagate(Lowres **frames, double average_duration, int p0, int p1, int b, int referenced);
    void    cuTreeFinish(Lowres *frame, double averageDuration, int ref0Distance);
    void    computeCUTreeQpOffset(Lowres *frame, double averageDuration, int ref0Distance);

    /* called by getEstimatedPictureCost() to finalize cuTree costs */
    int64_t frameCostRecalculate(Lowres **frames, int p0, int p1, int b);
    /*Compute index for positioning B-Ref frames*/
    void     placeBref(Frame** frames, int start, int end, int num, int *brefs);
    void     compCostBref(Lowres **frame, int start, int end, int num);
};

class PreLookaheadGroup : public BondedTaskGroup
{
public:

    Frame* m_preframes[X265_LOOKAHEAD_MAX];
    Lookahead& m_lookahead;

    PreLookaheadGroup(Lookahead& l) : m_lookahead(l) {}

    void processTasks(int workerThreadID);

protected:

    PreLookaheadGroup& operator=(const PreLookaheadGroup&);
};

struct LookaheadSlot
{
    LookaheadTLD tld;
    MotionEstimatorTLD metld;
    LookaheadSlot* next;   // free-list link
};

class TLDFreeList
{
public:
    void init(int n, int w, int h, int blocks)
    {
        m_width = w; m_height = h; m_blocks = blocks;
        m_slots = new LookaheadSlot[n];
        for (int i = 0; i < n; i++)
        {
            m_slots[i].tld.init(w, h, blocks);
            m_slots[i].next = (i + 1 < n) ? &m_slots[i + 1] : nullptr;
        }
        m_free.store(&m_slots[0], std::memory_order_relaxed);
        m_count = n;
    }
    ~TLDFreeList() { delete[] m_slots; }

    /* May return NULL when deep work-stealing chains exceed the arena size;
     * callers fall back to a heap-allocated slot (see ScopedTLD). A spinlock
     * guards the free list: a naive CAS stack is ABA-prone here, and the lock
     * is uncontended relative to the slice/frame-estimate task granularity */
    LookaheadSlot* acquire()
    {
        while (m_lock.test_and_set(std::memory_order_acquire))
            ;
        LookaheadSlot* head = m_free.load(std::memory_order_relaxed);
        if (head)
            m_free.store(head->next, std::memory_order_relaxed);
        m_lock.clear(std::memory_order_release);
        return head;
    }

    LookaheadSlot* newSlot() const
    {
        LookaheadSlot* s = new LookaheadSlot;
        s->tld.init(m_width, m_height, m_blocks);
        s->next = nullptr;
        return s;
    }

    void release(LookaheadSlot* s)
    {
        while (m_lock.test_and_set(std::memory_order_acquire))
            ;
        s->next = m_free.load(std::memory_order_relaxed);
        m_free.store(s, std::memory_order_relaxed);
        m_lock.clear(std::memory_order_release);
    }

    LookaheadSlot* m_slots = nullptr;
    std::atomic<LookaheadSlot*> m_free{nullptr};
    std::atomic_flag m_lock = ATOMIC_FLAG_INIT;
    int m_count = 0;
    int m_width = 0, m_height = 0, m_blocks = 0;
};

struct ScopedTLD   // RAII, put at top of every leaf task body
{
    ScopedTLD(TLDFreeList& a) : arena(a), slot(a.acquire()), owned(false)
    {
        if (!slot)   // arena exhausted by a deep steal chain; rare slow path
        {
            slot = a.newSlot();
            owned = true;
        }
    }
    ~ScopedTLD()
    {
        if (owned)
            delete slot;
        else
            arena.release(slot);
    }
    LookaheadTLD& tld() { return slot->tld; }
    MotionEstimatorTLD& metld() { return slot->metld; }
    TLDFreeList& arena; LookaheadSlot* slot; bool owned;
};

/* Fork-join scheduler for lookahead cost estimation. Fully independent of
 * x265's ThreadPool: it owns its own libfork lazy_pool workers and a TLD
 * arena sized for the worst-case number of concurrently live tasks. */
class ForkJoinPool
{
public:

    enum { MAX_COOP_SLICES = 32 };
    enum { MAX_BATCH_SIZE = 512 };

    /* Batch frame cost / motion search estimates */
    struct Estimate
    {
        int p0, b, p1;
    };

    /* Batched MCSTF motion search rows (decoupled from frame cost estimates) */
    struct McstfTask
    {
        int    refIdx;
        int    blockRow;
        int    level;
        Frame* frame;
    };

    /* Worker count is derived from the encoder params only, never from
     * x265's ThreadPool; also used by Lookahead's constructor to gate
     * batch/coop-slice heuristics before the pool exists */
    static int workerCount(const x265_param* param);

    ForkJoinPool(Lookahead& lookahead);
    ~ForkJoinPool();

    void    add(Lowres** frames, int p0, int p1, int b);
    void    finishBatch();

    void    addMcstfRow(int refIdx, int blockRow, int level, Frame* frame);
    void    finishMcstfBatch();

    /* Cross-decision speculation: hand a predicted batch of estimates to the
     * pump thread, which runs it on the pool while the lookahead thread is
     * busy elsewhere (output queueing, the next pre-lookahead). quiesce()
     * must retire any in-flight batch before per-frame cost state is touched
     * again; a wrong prediction only wastes idle pool time, never changes
     * output (results are memoized identically) */
    void    speculate(Lowres** frames, int frameCount, const Estimate* est, int count);
    void    quiesce();

    Lookahead&    m_lookahead;
    int           m_numWorkers;
    lf::lazy_pool m_lf_pool;
    TLDFreeList   m_arena;

    Estimate      m_estimates[MAX_BATCH_SIZE];
    int           m_batchSize;
    Lowres**      m_batchFrames;

    McstfTask     m_mcstfTasks[MAX_BATCH_SIZE];
    int           m_mcstfSize;

    std::thread             m_specThread;
    std::mutex              m_specLock;
    std::condition_variable m_specCv;
    bool                    m_specExit;
    bool                    m_specBusy;
    int                     m_specCount;
    Lowres*                 m_specFrames[X265_LOOKAHEAD_MAX + X265_BFRAME_MAX + 4];
    Estimate                m_specEstimates[MAX_BATCH_SIZE];

    void    specLoop();

protected:

    ForkJoinPool& operator=(const ForkJoinPool&);
};

/* General frame cost query: runs an estimateFrameCost coroutine on the
 * fork-join pool and blocks the calling (lookahead) thread for the result */
int64_t singleCost(ForkJoinPool& fjp, Lowres** frames, int p0, int p1, int b, bool intraPenalty = false);

bool computeEdge(pixel* edgePic, const pixel* refPic, pixel* edgeTheta, intptr_t stride, int height, int width, bool bcalcTheta, pixel whitePixel = (pixel)EDGE_THRESHOLD, int32_t* gradMag = NULL);
}
#endif // ifndef X265_SLICETYPE_H
