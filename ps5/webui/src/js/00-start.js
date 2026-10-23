(function () {
  var PRESETS = [[512, 512], [512, 768], [768, 512], [640, 640], [768, 768], [768, 1024], [1024, 768], [1024, 1024]];
  var PAGE = 24;
  var SURPRISE = [
    'a lighthouse on a rocky coast at dusk, dramatic clouds, oil painting',
    'a red fox sleeping in a snowy forest, soft morning light',
    'a cozy cabin in the mountains, warm window light, starry sky',
    'a futuristic city at night, neon reflections on wet streets',
    'a astronaut riding a horse on mars, cinematic',
    'a bowl of ramen, steam rising, studio photo, shallow depth of field',
    'a tiny robot watering flowers in a greenhouse, watercolor',
    'a sailing ship inside a glass bottle on a wooden desk',
    'an ancient temple overgrown with jungle, golden hour'
  ];
  var $ = function (id) { return document.getElementById(id); };

  var st = {
    useJobs: false,      // /sdcpp/v1/img_gen is available (job polling with previews)
    video: false,        // the loaded model makes videos (capabilities: vid_gen, no img_gen)
    vidUrl: null,        // the blob URL of the video on the stage
    canCancelGen: false, // the server can stop a job that is already generating
    busy: false,
    online: false,
    offFails: 0,
    others: false,       // the server is busy with someone else's job
    infoBusy: false,
    resumed: false,
    job: null,           // id of the async job being watched
    token: 0,            // bumps when a run is over, so late replies are ignored
    t0: 0,               // when the run started (ms)
    seed: 0,
    fails: 0,
    limit: PAGE,
    sub: '',
    model: null,         // name of the loaded model (null = not asked yet)
    label: '',           // what to show for it: the library name, else the model name
    mid: '',             // id of the loaded library entry ('' = unknown)
    mkey: null,          // whose settings the form holds (the id, else the name)
    announce: '',        // name of the model just switched to (for the toast)
    app: null,           // the app's model routes exist (null = not asked yet)
    mdl: null,           // last answer of GET /ps5sd/v1/app/models
    sw: null,            // a model switch under way: {id, name, t0, ticks, polling, down, waitJob}
    dl: null,            // the download routes exist (null = not asked yet)
    cat: null,           // the catalog route exists
    hf: null,            // the Hugging Face search route exists
    delErr: null,        // why the last delete failed: {id, msg}, shown in that row
    dlActive: 0,         // downloads that are queued or running
    view: 'create',
    preset: null,
    last: null,          // settings of the last run
    maxRepeat: 100000,   // images per run (the count next to Generate): limits.max_repeat, else the page makes the run
    serverRepeat: false, // the server makes a run itself ("repeat" on img_gen)
    run: null,           // the run on screen: {total, done, server, items, ...} (09b-runs-and-queue.js)
    runMode: false,      // st.results holds a run's images (the strip opens them in the viewer)
    stageItem: null,     // the finished image of a run that the stage shows (null while a preview is shown)
    shownDims: null,     // the size of the last finished image (a live preview is drawn at the size of the final one)
    waiting: 0,          // jobs queued on the server, after the one running
    queueBusy: false,    // a GET /ps5sd/v1/jobs is under way
    queued: {},          // the request of each job this page queued, by job id (for when it is followed)
    gone: {},            // jobs this page stopped waiting for: the queue does not take them up again
    followPrompt: '',    // the prompt of a job this page follows but did not start
    ticking: false,      // the clock of the result line runs (see tick())
    results: null,       // the last run's images: [{src, seed, url, name}]
    pick: 0,             // which of them is shown
    sizes: null,         // the size buttons: built-in and own
    audio: null,         // AudioContext for the done sound (made on a click)
    title: '',           // the page title without the done mark
    known: null,         // gallery names before the current run
    lb: -1,
    cn: null,            // the guide image (ControlNet): {canvas, name}
    vinit: null,         // the start image of a video (Wan 2.2 TI2V): {canvas, name}
    cnModel: '',         // the loaded entry's ControlNet file, '' for none
    mode: 'image',       // what the request is made from: image, edit (st.init), guide (st.cn), video
    pace: null           // the step timing of the run on screen (see pace() in 09b-runs-and-queue.js)
  };

