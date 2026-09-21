# Monthly item effects

The `items` rules support two integer properties, both defaulting to `0`:

- `monthlyScore`: rating added at the end of each month per item owned.
- `monthlyTension`: tension added at the end of each month per item owned.

Both accept negative values. Effects are multiplied by the quantity present at
the end of the month and summed across all player bases. Items in base stores,
craft inventories (including craft in flight), and pending item transfers count.
The inventory of a craft being transferred also counts. Pending purchases use
the same transfer system and count as well. Installed equipment and worn armor
are not inventory items for this calculation.

Effects are applied to the ending month's global rating and tension before the
monthly report and the next month's mission scripts are processed.

```yaml
items:
  - type: STR_EXAMPLE_ITEM
    monthlyScore: 10
    monthlyTension: -2
```

Three copies of this item add 30 rating and subtract 6 tension each month.
