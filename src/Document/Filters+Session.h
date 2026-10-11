// Swift's Filters extension: one filter at a time.
public:
    bool canContentAwareFill() const;
    // `repeating` is Last Filter: no preview unless automatic, no panel.
    void beginFilter(FilterKind kind, bool repeating = false);
    // What Filter › Last Filter runs again.
    std::optional<FilterKind> lastFilter() const { return m_lastFilter; }
    void setLastFilter(std::optional<FilterKind> kind);
    bool canRepeatLastFilter() const;
    // The last filter again, its settings kept, one step.
    void repeatLastFilter(std::function<void()> done = {});
    // The settings normalized; Preview off drops the queue and preview.
    void updateFilter(const FilterSettings &settings, bool preview);
    void cancelFilter();
    // Off the UI thread; `done` follows the step or refusal.
    void commitFilter(std::function<void()> done = {});

private:
    void renderFilterPreview();
    void finishFilterPreview();
    void finishFilterCommit();
    // Swift's commitBackgroundMask: the subject kept by a mask.
    void commitBackgroundMask(const FilterEdit &edit);
